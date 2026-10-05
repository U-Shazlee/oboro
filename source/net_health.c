#include "net_health.h"

#include <string.h>

#include "diagnostic.h"

/* Limits for a 30 fps stream of about 1 Mbps. Ping is the round trip of the
 * control stream, so it covers both directions. */
enum {
    PING_WARN_MS = 80,
    PING_BAD_MS = 150,
    JITTER_WARN_MS = 20,
    JITTER_BAD_MS = 50,
    /* Tenths of a percent of video packets. */
    LOSS_WARN = 10,
    LOSS_BAD = 50,
    FPS_WARN = 28,
    FPS_BAD = 22,
    /* The console may show this many frames fewer than arrive (30.00 fps
     * against the LCD's 29.92, and the pacer's drops). */
    SHOWN_SLACK = 3,
    /* A still picture: the PC sends fewer frames by design and far less
     * data. Below this share of the asked bitrate a low frame rate is not
     * held against the PC. */
    MOVING_PERCENT = 35,
    /* A fault stays on screen this long, so one bad second can be read. */
    HOLD_SECONDS = 3
};

static NetHealth g_health;
static NetHealthSample g_last;
static bool g_have_last;
static NetLevel g_held_level;
static NetCause g_held_cause;
static unsigned g_held_for;

void net_health_reset(void)
{
    memset(&g_health, 0, sizeof(g_health));
    g_have_last = false;
    g_held_level = NET_GOOD;
    g_held_cause = NET_CAUSE_NONE;
    g_held_for = 0;
}

static unsigned since(unsigned now, unsigned before)
{
    return now >= before ? now - before : 0;
}

static NetLevel rate_high(int value, int warn, int bad)
{
    return value >= bad ? NET_BAD : value >= warn ? NET_WARN : NET_GOOD;
}

static void consider(NetLevel *level, NetCause *cause, NetLevel found, NetCause why)
{
    if (found > *level) {
        *level = found;
        *cause = why;
    }
}

void net_health_sample(const NetHealthSample *s)
{
    NetHealth *h = &g_health;
    /* A total that went backwards: the stream reconnected. */
    if (g_have_last && (s->video_packets < g_last.video_packets || s->input_sent < g_last.input_sent))
        g_have_last = false;
    if (!g_have_last) {
        g_last = *s;
        g_have_last = true;
        return;
    }
    const unsigned packets = since(s->video_packets, g_last.video_packets);
    const unsigned recovered = since(s->recovered_packets, g_last.recovered_packets);
    const unsigned failed = since(s->failed_packets, g_last.failed_packets);
    const unsigned frames_lost = since(s->frames_lost, g_last.frames_lost);
    const unsigned input_failed = since(s->input_failed, g_last.input_failed);
    h->input_per_second = since(s->input_sent, g_last.input_sent);
    g_last = *s;

    h->wifi_bars = s->wifi_bars;
    h->ping_ms = s->rtt_ms;
    h->jitter_ms = s->rtt_variance_ms;
    h->kbps = s->kbps;
    h->target_kbps = s->target_kbps;
    h->pc_fps = s->pc_fps;
    h->shown_fps = s->shown_fps;
    h->unrecovered = failed;
    h->input_failed = input_failed;
    /* Every rebuilt or unrebuildable packet was one that went missing. */
    const unsigned missing = recovered + failed;
    h->loss_permille = packets + missing ? missing * 1000u / (packets + missing) : 0;

    h->ping_level = rate_high(s->rtt_ms, PING_WARN_MS, PING_BAD_MS);
    const NetLevel jitter_level = rate_high(s->rtt_variance_ms, JITTER_WARN_MS, JITTER_BAD_MS);
    h->loss_level = rate_high((int)h->loss_permille, LOSS_WARN, LOSS_BAD);
    /* What error correction could not rebuild shows on screen. */
    if (failed && h->loss_level < NET_WARN) h->loss_level = NET_WARN;
    if (frames_lost) h->loss_level = NET_BAD;
    const bool moving = s->kbps * 100u >= s->target_kbps * MOVING_PERCENT;
    h->pc_level = !moving ? NET_GOOD : s->pc_fps < FPS_BAD ? NET_BAD : s->pc_fps < FPS_WARN ? NET_WARN : NET_GOOD;
    h->input_level = input_failed ? NET_BAD : NET_GOOD;

    /* The verdict: the worst measurement, and among equals the one nearest
     * the root. Loss first (it also lowers the frame rate), then delay, then
     * a PC that sends too little, then the console itself. */
    NetLevel level = NET_GOOD;
    NetCause cause = NET_CAUSE_NONE;
    if (!s->pc_fps && !packets) {
        level = NET_BAD;
        cause = NET_CAUSE_NO_VIDEO;
    } else {
        consider(&level, &cause, h->loss_level, s->wifi_bars <= 1 ? NET_CAUSE_WIFI : NET_CAUSE_LOSS);
        consider(&level, &cause, h->ping_level, NET_CAUSE_PING);
        consider(&level, &cause, jitter_level, NET_CAUSE_JITTER);
        /* Few frames with nothing lost: they were never sent. */
        if (h->loss_level == NET_GOOD) consider(&level, &cause, h->pc_level, NET_CAUSE_PC);
        if (s->shown_fps + SHOWN_SLACK < s->pc_fps)
            consider(&level, &cause, s->shown_fps + 2 * SHOWN_SLACK < s->pc_fps ? NET_BAD : NET_WARN,
                     NET_CAUSE_CONSOLE);
        consider(&level, &cause, h->input_level, NET_CAUSE_INPUT);
        /* A weak signal with nothing wrong yet is still worth a warning. */
        if (!s->wifi_bars) consider(&level, &cause, NET_WARN, NET_CAUSE_WIFI);
    }

    if (h->history_count == NET_HISTORY) {
        memmove(h->history, h->history + 1, NET_HISTORY - 1);
        --h->history_count;
    }
    h->history[h->history_count++] = (unsigned char)level;

    const NetCause shown_before = h->cause;
    if (level >= g_held_level || g_held_for >= HOLD_SECONDS) {
        g_held_level = level;
        g_held_cause = cause;
        g_held_for = 0;
    } else {
        ++g_held_for;
    }
    h->level = g_held_level;
    h->cause = g_held_cause;
    if (h->cause != shown_before || !h->valid)
        diagnostic_log("NET", "health: %s (ping %d ms +-%d, loss %u.%u%%, unrecovered %u, pc %u fps, shown %u fps, "
                       "%u kbps of %u, wifi %u/3)", net_health_verdict(h), h->ping_ms, h->jitter_ms,
                       h->loss_permille / 10, h->loss_permille % 10, h->unrecovered, h->pc_fps, h->shown_fps,
                       h->kbps, h->target_kbps, h->wifi_bars);
    h->valid = true;
}

const NetHealth *net_health_get(void) { return &g_health; }

const char *net_health_verdict(const NetHealth *h)
{
    switch (h->cause) {
    case NET_CAUSE_NO_VIDEO: return "NO PICTURE ARRIVING";
    case NET_CAUSE_WIFI: return "WEAK WI-FI SIGNAL";
    case NET_CAUSE_LOSS: return "PACKETS BEING LOST";
    case NET_CAUSE_PC: return "PC SENDING TOO FEW";
    case NET_CAUSE_PING: return "HIGH DELAY";
    case NET_CAUSE_JITTER: return "UNSTEADY DELAY";
    case NET_CAUSE_CONSOLE: return "3DS FALLING BEHIND";
    case NET_CAUSE_INPUT: return "INPUT NOT SENDING";
    case NET_CAUSE_NONE: break;
    }
    return "SMOOTH";
}
