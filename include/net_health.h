#pragma once

#include <stdbool.h>

/* Stream health for the Network stats page: once a second the stream's raw
 * counters become a few rated measurements, one verdict naming the most
 * likely cause of trouble, and a minute of history. UI thread only. */

typedef enum { NET_GOOD, NET_WARN, NET_BAD } NetLevel;

/* Where the trouble is, most specific first. */
typedef enum {
    NET_CAUSE_NONE,
    /* Nothing arrives at all. */
    NET_CAUSE_NO_VIDEO,
    /* Packets are lost and the console's own Wi-Fi signal is weak. */
    NET_CAUSE_WIFI,
    /* Packets are lost somewhere between the PC and the console. */
    NET_CAUSE_LOSS,
    /* Few frames arrive although nothing is lost: the PC sends too few. */
    NET_CAUSE_PC,
    NET_CAUSE_PING,
    /* The delay itself is fine but swings. */
    NET_CAUSE_JITTER,
    /* Frames arrive but the console shows fewer. */
    NET_CAUSE_CONSOLE,
    /* The console's own messages to the PC are not getting out. */
    NET_CAUSE_INPUT
} NetCause;

#define NET_HISTORY 60

/* Running totals as the stream reports them; a total that went down means a
 * new connection and restarts the differences. */
typedef struct {
    unsigned wifi_bars;
    int rtt_ms, rtt_variance_ms;
    unsigned kbps, target_kbps;
    /* Frames that arrived from the PC, and frames shown, this second. */
    unsigned pc_fps, shown_fps;
    unsigned video_packets, recovered_packets, failed_packets;
    unsigned frames_lost;
    unsigned input_sent, input_failed;
} NetHealthSample;

typedef struct {
    /* At least one second has been measured. */
    bool valid;
    NetLevel level;
    NetCause cause;
    unsigned wifi_bars;
    int ping_ms, jitter_ms;
    NetLevel ping_level;
    /* Video packets that went missing, in tenths of a percent; `unrecovered`
     * counts those error correction could not rebuild this second. */
    unsigned loss_permille, unrecovered;
    NetLevel loss_level;
    unsigned pc_fps, shown_fps;
    NetLevel pc_level;
    /* Informational: a still picture needs little data, so a low rate alone
     * is not a fault. */
    unsigned kbps, target_kbps;
    unsigned input_per_second, input_failed;
    NetLevel input_level;
    /* One NetLevel a second, oldest first. */
    unsigned char history[NET_HISTORY];
    unsigned history_count;
} NetHealth;

/* A new stream: forget the history and the baselines. */
void net_health_reset(void);
void net_health_sample(const NetHealthSample *sample);
const NetHealth *net_health_get(void);
/* The verdict in a few words ("SMOOTH", "WEAK WI-FI SIGNAL"...). */
const char *net_health_verdict(const NetHealth *health);
