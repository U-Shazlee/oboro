#pragma once

/* Background thread that owns every blocking network call (pairing, the app
 * list, launching, the stream handshake, updates), so the UI and the stream
 * never wait on the network. The UI thread reads a published snapshot of the
 * client state and never mutates it directly. */

#include <stdbool.h>

#include "host_client.h"
#include "moon_transport.h"

typedef enum {
    NET_JOB_NONE,
    /* Text: the PC's address. */
    NET_JOB_BEGIN_LOGIN,
    NET_JOB_CANCEL_LOGIN,
    NET_JOB_LOAD_LIBRARY,
    NET_JOB_START_SESSION,
    NET_JOB_STOP_SESSION,
    /* Connect the stream of a launched app (the Moonlight handshake). */
    NET_JOB_START_STREAM,
    NET_JOB_SIGN_OUT,
    /* Text "beta" includes pre-releases. */
    NET_JOB_UPDATE_CHECK,
    NET_JOB_UPDATE_INSTALL,
    /* Opt-in diagnostic report (report.h). */
    NET_JOB_SEND_REPORT,
    /* Anonymous session performance summary (perf_stats.h). */
    NET_JOB_SEND_STATS,
    /* Another app runs on the PC: quit it and launch. */
    NET_JOB_END_CONFLICT,
    /* The stream dropped: launch again, which resumes the running app. */
    NET_JOB_RECOVER
} NetJobKind;

typedef struct {
    NetJobKind kind;
    bool ok;
    bool cancelled;
    unsigned serial;
} NetJobResult;

/* Takes a copy of the initialised client; `transport` is connected by the
 * worker for NET_JOB_START_STREAM and otherwise belongs to the UI thread. */
bool net_worker_start(const HostClient *client, MoonTransport *transport);
void net_worker_stop(void);
/* Queue one job; false while another job is pending or running. */
bool net_worker_submit(NetJobKind kind, const char *text, const HostGame *game);
bool net_worker_busy(void);
NetJobKind net_worker_current_job(void);
/* Abort the HTTP request in flight for the current job. */
void net_worker_cancel(void);
/* Copy the newest published client state into `out` if it changed. */
bool net_worker_sync(HostClient *out);
NetJobResult net_worker_last_result(void);
/* Block until the current job finishes or the timeout passes. */
void net_worker_wait_idle(unsigned timeout_ms);
