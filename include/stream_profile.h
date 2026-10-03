#pragma once

#include <stdbool.h>

/* What the PC is asked to encode. Chosen in Settings > Picture and applied
 * at the next launch; the launch request and the decoder both read it from
 * here so they can never disagree. */

/* 3DS Wi-Fi (2.4 GHz 802.11g) is the limit, not the PC: Kasumi measured
 * ~1 Mbps with almost no loss and bursts of loss from ~1.5 Mbps on a weaker
 * signal. Balanced is therefore the default and the other rates are opt-in.
 * The names are kept from Kasumi; saved settings store the index. */
typedef enum {
    /* Balanced: ~1.4 Mbps. */
    STREAM_BITRATE_ADAPTIVE,
    STREAM_BITRATE_STEADY_1000,
    STREAM_BITRATE_STEADY_1200,
    STREAM_BITRATE_STEADY_1500,
    /* Sharp: ~2 Mbps, for strong Wi-Fi. */
    STREAM_BITRATE_SHARP_TEST,
    STREAM_BITRATE_COUNT
} StreamBitrateMode;

void stream_profile_configure(bool wide, StreamBitrateMode bitrate);
bool stream_profile_wide(void);
const char *stream_profile_name(void);
unsigned stream_profile_width(void);
unsigned stream_profile_height(void);
/* The bitrate asked of the PC, in kbps. */
unsigned stream_profile_initial_bitrate(void);
/* Sharp is running: its sessions don't mark a network as choppy. */
bool stream_profile_test_mode(void);
/* Weak Wi-Fi / phone hotspot: 0.8 Mbps whatever the bitrate setting (fewer
 * packets per frame, so fewer frames hit by a loss) and a bigger frame
 * reserve. */
void stream_profile_set_weak(bool weak);
bool stream_profile_weak(void);
