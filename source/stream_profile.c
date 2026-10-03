#include "stream_profile.h"

#include <stdio.h>

static bool g_wide = true;
static StreamBitrateMode g_bitrate = STREAM_BITRATE_ADAPTIVE;
static bool g_weak;

void stream_profile_configure(bool wide, StreamBitrateMode bitrate)
{
    g_wide = wide;
    g_bitrate = bitrate < STREAM_BITRATE_COUNT ? bitrate : STREAM_BITRATE_ADAPTIVE;
}

bool stream_profile_wide(void) { return g_wide; }

const char *stream_profile_name(void)
{
    static char name[48];
    snprintf(name, sizeof(name), "%ux%u · %u.%u Mbps%s", stream_profile_width(), stream_profile_height(),
             stream_profile_initial_bitrate() / 1000, stream_profile_initial_bitrate() % 1000 / 100,
             g_weak ? " (weak link)" : "");
    return name;
}

/* A PC encodes any size it is asked for, so the stream is exactly what the
 * top screen shows and MVD has nothing to scale: 800x480 for the 800-column
 * wide mode (as Moonlight-N3DS), 400x240 for the classic screen. */
unsigned stream_profile_width(void) { return g_wide ? 800 : 400; }
unsigned stream_profile_height(void) { return g_wide ? 480 : 240; }

void stream_profile_set_weak(bool weak) { g_weak = weak; }
bool stream_profile_weak(void) { return g_weak; }

unsigned stream_profile_initial_bitrate(void)
{
    if (g_weak) return 800;
    switch (g_bitrate) {
    case STREAM_BITRATE_STEADY_1000: return 1000;
    case STREAM_BITRATE_STEADY_1200: return 1200;
    case STREAM_BITRATE_STEADY_1500: return 1500;
    case STREAM_BITRATE_SHARP_TEST: return 2000;
    default: return 1400;
    }
}

bool stream_profile_test_mode(void) { return g_bitrate == STREAM_BITRATE_SHARP_TEST; }
