#include "net_worker.h"

#include <3ds.h>
#include <stdio.h>
#include <string.h>

#include "diagnostic.h"
#include "http_client.h"
#include "game_art.h"
#include "pc_stats.h"
#include "report.h"
#include "updater.h"

#define WORKER_STACK_SIZE (128 * 1024)
#define WORKER_IDLE_NS 50000000LL

/* The worker mutates g_work; the UI copies g_shared. Both are ~50 KiB, so
 * they live in static storage rather than on either thread's stack. */
static HostClient g_work;
static HostClient g_shared;
static MoonTransport *g_transport;
static unsigned g_version;
static LightLock g_lock;
static Thread g_thread;
static volatile bool g_quit;

static NetJobKind g_pending;
static char g_pending_text[80];
static HostGame g_pending_game;
static volatile NetJobKind g_running;
static volatile bool g_cancelled;
static NetJobResult g_result;

static void publish(void)
{
    LightLock_Lock(&g_lock);
    memcpy(&g_shared, &g_work, sizeof(g_shared));
    ++g_version;
    LightLock_Unlock(&g_lock);
}

static bool run_job(NetJobKind kind, const char *text, const HostGame *game)
{
    switch (kind) {
    case NET_JOB_BEGIN_LOGIN: {
        char address[sizeof(g_pending_text)];
        snprintf(address, sizeof(address), "%s", text);
        char *key = strchr(address, ' ');
        if (key) *key++ = '\0';
        return host_begin_login(&g_work, address, key ? key : "");
    }
    case NET_JOB_SET_HOST_KEY:
        if (!host_set_key(&g_work, text)) return false;
        game_art_prefetch(g_work.games, (unsigned)g_work.game_count);
        return true;
    case NET_JOB_CANCEL_LOGIN:
        g_work.auth_state = HOST_AUTH_LOGGED_OUT;
        g_work.pair_pending = false;
        snprintf(g_work.status, sizeof(g_work.status), "Pairing cancelled");
        return true;
    case NET_JOB_LOAD_LIBRARY:
        if (!host_fetch_library(&g_work)) return false;
        game_art_prefetch(g_work.games, (unsigned)g_work.game_count);
        return true;
    case NET_JOB_UPDATE_CHECK: return updater_check(!strcmp(text, "beta"));
    case NET_JOB_UPDATE_INSTALL: return updater_install();
    case NET_JOB_SEND_REPORT: return report_send(text);
    case NET_JOB_SEND_STATS: return report_send_stats();
    /* A dropped stream reconnects the same way a launch starts: the PC
     * resumes the app when it is still running. */
    case NET_JOB_START_SESSION: return host_start_session(&g_work, game, true);
    case NET_JOB_RECOVER: return host_start_session(&g_work, game, false);
    case NET_JOB_STOP_SESSION: return host_stop_session(&g_work);
    case NET_JOB_START_STREAM: {
        /* The UI sees "connecting" while the handshake blocks here. */
        publish();
        const bool ok = host_stream_start(&g_work, g_transport);
        snprintf(g_work.status, sizeof(g_work.status), "%s", g_transport->status);
        return ok;
    }
    case NET_JOB_END_CONFLICT: return host_end_conflict(&g_work, game);
    case NET_JOB_SIGN_OUT:
        host_sign_out(&g_work);
        return true;
    case NET_JOB_NONE: break;
    }
    return false;
}

static void worker_main(void *arg)
{
    (void)arg;
    while (!g_quit) {
        LightLock_Lock(&g_lock);
        const NetJobKind kind = g_pending;
        char text[sizeof(g_pending_text)];
        HostGame game;
        memcpy(text, g_pending_text, sizeof(text));
        memcpy(&game, &g_pending_game, sizeof(game));
        g_pending = NET_JOB_NONE;
        if (kind != NET_JOB_NONE) {
            g_running = kind;
            g_cancelled = false;
        }
        LightLock_Unlock(&g_lock);

        if (kind != NET_JOB_NONE) {
            const u64 started = osGetTime();
            const bool ok = run_job(kind, text, &game);
            diagnostic_log("WORKER", "job=%d ok=%d cancelled=%d ms=%llu", (int)kind, ok ? 1 : 0,
                           g_cancelled ? 1 : 0, (unsigned long long)(osGetTime() - started));
            LightLock_Lock(&g_lock);
            g_result.kind = kind;
            g_result.ok = ok;
            g_result.cancelled = g_cancelled;
            ++g_result.serial;
            g_running = NET_JOB_NONE;
            LightLock_Unlock(&g_lock);
            publish();
            continue;
        }
        /* Periodic work: the pairing request waits here for the PIN. */
        const HostAuthState auth_before = g_work.auth_state;
        host_tick(&g_work);
        if (g_work.auth_state != auth_before) {
            publish();
            if (g_work.auth_state == HOST_AUTH_LOGGED_IN)
                game_art_prefetch(g_work.games, (unsigned)g_work.game_count);
        }
        /* The client snapshot is ~130 KiB; copying it 20 times a second
         * (and again on the UI thread) was wasted work during play. */
        static u64 last_publish;
        if (osGetTime() - last_publish >= 200) {
            publish();
            last_publish = osGetTime();
        }
        /* While a game streams, the PC's own numbers for the stats tiles. */
        if (host_session_active(&g_work)) pc_stats_poll(g_work.address, g_work.host_key);
        /* Box art only downloads while no game is launching or running, so
         * it never competes with the stream for Wi-Fi. */
        if (!host_session_active(&g_work) && game_art_work()) continue;
        svcSleepThread(WORKER_IDLE_NS);
    }
}

bool net_worker_start(const HostClient *client, MoonTransport *transport)
{
    LightLock_Init(&g_lock);
    memcpy(&g_work, client, sizeof(g_work));
    memcpy(&g_shared, client, sizeof(g_shared));
    g_transport = transport;
    g_quit = false;
    s32 priority = 0x30;
    svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
    /* One step below the UI thread on the same core: it runs whenever the UI
     * waits for vblank or naps between network polls, and never preempts it. */
    g_thread = threadCreate(worker_main, NULL, WORKER_STACK_SIZE, priority + 1, -2, false);
    return g_thread != NULL;
}

void net_worker_stop(void)
{
    if (!g_thread) return;
    g_quit = true;
    http_cancel();
    host_cancel();
    threadJoin(g_thread, U64_MAX);
    threadFree(g_thread);
    g_thread = NULL;
}

bool net_worker_submit(NetJobKind kind, const char *text, const HostGame *game)
{
    LightLock_Lock(&g_lock);
    const bool accepted = g_pending == NET_JOB_NONE && g_running == NET_JOB_NONE;
    if (accepted) {
        g_pending = kind;
        snprintf(g_pending_text, sizeof(g_pending_text), "%s", text ? text : "");
        if (game) memcpy(&g_pending_game, game, sizeof(g_pending_game));
        /* Report busy immediately, before the worker picks the job up. */
        g_running = kind;
    }
    LightLock_Unlock(&g_lock);
    return accepted;
}

bool net_worker_busy(void) { return g_running != NET_JOB_NONE; }
NetJobKind net_worker_current_job(void) { return g_running; }

void net_worker_cancel(void)
{
    g_cancelled = true;
    http_cancel();
    host_cancel();
}

bool net_worker_sync(HostClient *out)
{
    static unsigned seen = ~0u;
    LightLock_Lock(&g_lock);
    const bool changed = g_version != seen;
    if (changed) {
        memcpy(out, &g_shared, sizeof(*out));
        seen = g_version;
    }
    LightLock_Unlock(&g_lock);
    return changed;
}

NetJobResult net_worker_last_result(void)
{
    LightLock_Lock(&g_lock);
    const NetJobResult result = g_result;
    LightLock_Unlock(&g_lock);
    return result;
}

void net_worker_wait_idle(unsigned timeout_ms)
{
    const u64 deadline = osGetTime() + timeout_ms;
    while (net_worker_busy() && osGetTime() < deadline) svcSleepThread(10000000LL);
}
