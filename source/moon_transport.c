#include "moon_transport.h"

#include <3ds.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <Limelight.h>

#include "audio_output.h"
#include "diagnostic.h"
#include "h264_sps.h"
#include "host_input.h"
#include "mvd_video.h"
#include "stream_profile.h"

enum {
    /* Button and trigger edges go out within 4 ms; stick motion changes
     * almost every poll, so it stays at 8 ms to keep the control stream
     * calm. The channel is reliable, so an unchanged state is not repeated. */
    INPUT_BUTTON_INTERVAL_MS = 4,
    INPUT_MIN_INTERVAL_MS = 8,
    /* Windows virtual key for Shift, pressed around capitals and symbols. */
    VK_LSHIFT_CODE = 0xA0
};

/* The one transport the callbacks report into (moonlight-common-c keeps a
 * single connection per process). */
static MoonTransport *g_transport;
static RecursiveLock g_lock;
static bool g_lock_ready;
static volatile bool g_starting;
static volatile bool g_started;
static volatile bool g_paused;
static volatile bool g_need_idr;
static int g_failed_stage;
static uint16_t g_audio_sequence;
/* One access unit, joined from the depacketizer's buffer chain. */
static unsigned char *g_unit;
static size_t g_unit_capacity;

void moon_lock(void) { if (g_lock_ready) RecursiveLock_Lock(&g_lock); }
void moon_unlock(void) { if (g_lock_ready) RecursiveLock_Unlock(&g_lock); }

void moon_pause(bool paused)
{
    /* What arrived while suspended is stale: start again from a keyframe. */
    if (!paused && g_paused) g_need_idr = true;
    g_paused = paused;
}

void moon_wait(MoonTransport *t, int timeout_ms)
{
    (void)t;
    svcSleepThread((s64)timeout_ms * 1000000LL);
}

void moon_init(MoonTransport *t)
{
    if (!g_lock_ready) {
        RecursiveLock_Init(&g_lock);
        g_lock_ready = true;
    }
    memset(t, 0, sizeof(*t));
}

/* ---- Video ----------------------------------------------------------------- */

static int video_setup(int format, int width, int height, int fps, void *context, int flags)
{
    (void)context; (void)flags;
    diagnostic_log("VIDEO", "host stream format=%04x %dx%d@%d", format, width, height, fps);
    return 0;
}

static int video_submit(PDECODE_UNIT unit)
{
    MoonTransport *t = g_transport;
    if (!t || g_paused || unit->fullLength <= 0) return DR_OK;
    if ((size_t)unit->fullLength > g_unit_capacity) {
        unsigned char *grown = realloc(g_unit, (size_t)unit->fullLength + 4096);
        if (!grown) return DR_NEED_IDR;
        g_unit = grown;
        g_unit_capacity = (size_t)unit->fullLength + 4096;
    }
    size_t size = 0;
    for (PLENTRY entry = unit->bufferList; entry; entry = entry->next) {
        if (size + (size_t)entry->length > g_unit_capacity) return DR_NEED_IDR;
        memcpy(g_unit + size, entry->data, (size_t)entry->length);
        size += (size_t)entry->length;
    }

    const bool has_idr = unit->frameType == FRAME_TYPE_IDR;
    if (has_idr) {
        t->video_saw_idr = true;
        t->video_idr_units++;
        g_need_idr = false;
        unsigned width, height, profile, level, refs;
        uint32_t signature;
        if (h264_sps_dimensions(g_unit, size, &width, &height, &profile, &level, &refs, &signature) &&
            signature != t->video_sps_signature) {
            t->video_sps_signature = signature;
            t->video_source_width = width;
            t->video_source_height = height;
            t->video_source_refs = refs;
            diagnostic_log("VIDEO", "SPS source=%ux%u profile=%u level=%u refs=%u hash=%08lx",
                           width, height, profile, level, refs, (unsigned long)signature);
        }
    } else if (g_need_idr) {
        return DR_NEED_IDR;
    }
    t->video_access_units++;
    t->video_rate_units++;
    t->video_bytes += size;
    t->host_latency = unit->frameHostProcessingLatency;
    const uint64_t now = osGetTime();
    if (!t->video_rate_started_at) {
        t->video_rate_started_at = now;
        t->video_rate_bytes = t->video_bytes;
        t->video_rate_units = 0;
    } else if (now - t->video_rate_started_at >= 1000) {
        const uint64_t elapsed = now - t->video_rate_started_at;
        t->video_kbps = (unsigned)(((t->video_bytes - t->video_rate_bytes) * 8u) / elapsed);
        t->video_fps = (unsigned)((uint64_t)t->video_rate_units * 1000u / elapsed);
        t->video_rate_started_at = now;
        t->video_rate_bytes = t->video_bytes;
        t->video_rate_units = 0;
    }
    if (t->video_access_units == 1) diagnostic_checkpoint();

    /* MVD takes the coded size: whole 16-pixel macroblocks. */
    const unsigned source_width = t->video_source_width ? t->video_source_width : stream_profile_width();
    const unsigned source_height = t->video_source_height ? t->video_source_height : stream_profile_height();
    const unsigned decode_width = (source_width + 15u) & ~15u;
    const unsigned decode_height = (source_height + 15u) & ~15u;
    int result = DR_OK;
    moon_lock();
    if (mvd_video_active() && (t->decoder_width != decode_width || t->decoder_height != decode_height)) {
        if (!has_idr) {
            moon_unlock();
            return DR_NEED_IDR;
        }
        diagnostic_log("VIDEO", "decoder reconfigure %ux%u -> %ux%u at IDR",
                       t->decoder_width, t->decoder_height, decode_width, decode_height);
        mvd_video_close();
    }
    if (!mvd_video_active()) {
        /* The decoder starts on a keyframe, or everything shows up grey. */
        const bool refused = t->decoder_failed_width == decode_width && t->decoder_failed_height == decode_height &&
                             now - t->decoder_failed_at < 2000;
        if (!has_idr || refused) {
            moon_unlock();
            return has_idr ? DR_OK : DR_NEED_IDR;
        }
        if (!mvd_video_init(decode_width, decode_height)) {
            t->decoder_failed_width = decode_width;
            t->decoder_failed_height = decode_height;
            t->decoder_failed_at = now;
            snprintf(t->status, sizeof(t->status), "Video arrived; %.140s", mvd_video_status());
            moon_unlock();
            return DR_OK;
        }
        t->decoder_failed_width = t->decoder_failed_height = 0;
    }
    t->decoder_width = decode_width;
    t->decoder_height = decode_height;
    if (!mvd_video_submit(g_unit, size))
        snprintf(t->status, sizeof(t->status), "Video arrived; %.140s", mvd_video_status());
    /* The decode queue overflowed and dropped frames: resync on a keyframe. */
    if (mvd_video_take_resync_request()) result = DR_NEED_IDR;
    moon_unlock();
    if (result == DR_NEED_IDR) t->keyframe_requests++;
    return result;
}

/* ---- Audio ----------------------------------------------------------------- */

static int audio_init(int configuration, const POPUS_MULTISTREAM_CONFIGURATION opus, void *context, int flags)
{
    (void)configuration; (void)context; (void)flags;
    g_audio_sequence = 0;
    diagnostic_log("AUDIO", "host audio %d Hz %d ch %d samples/packet", opus->sampleRate, opus->channelCount,
                   opus->samplesPerFrame);
    /* A console without sound still plays: never fail the stream for it. */
    audio_output_init();
    return 0;
}

static void audio_sample(char *data, int length)
{
    MoonTransport *t = g_transport;
    if (!t || g_paused || length <= 0) return;
    t->audio_packets++;
    /* Stereo arrives as one coupled Opus stream: a plain Opus packet. */
    const int result = audio_output_submit((const uint8_t *)data, (size_t)length, 0, g_audio_sequence++);
    if (result > 0) t->audio_decoded++;
    else if (result == 0) t->audio_dropped++;
    else t->audio_errors++;
}

/* ---- Connection events ----------------------------------------------------- */

static void on_stage_starting(int stage)
{
    MoonTransport *t = g_transport;
    if (t) snprintf(t->status, sizeof(t->status), "Connecting: %s", LiGetStageName(stage));
    diagnostic_log("MOON", "stage %s", LiGetStageName(stage));
}

static void on_stage_failed(int stage, int error)
{
    g_failed_stage = stage;
    diagnostic_log("MOON", "stage %s failed error=%d", LiGetStageName(stage), error);
}

static void on_terminated(int error)
{
    MoonTransport *t = g_transport;
    diagnostic_log("MOON", "connection terminated error=%d", error);
    if (!t) return;
    const char *reason;
    switch (error) {
    case ML_ERROR_GRACEFUL_TERMINATION: reason = "The app was closed on your PC."; break;
    case ML_ERROR_NO_VIDEO_TRAFFIC: reason = "No video arrived. Check the PC's firewall (UDP 47998-48000)."; break;
    case ML_ERROR_NO_VIDEO_FRAME: reason = "The connection is too weak for this bitrate. Try a lower one."; break;
    case ML_ERROR_UNEXPECTED_EARLY_TERMINATION: reason = "Your PC could not capture its screen."; break;
    case ML_ERROR_PROTECTED_CONTENT: reason = "Your PC is showing protected content and stopped the stream."; break;
    default: reason = NULL; break;
    }
    if (reason) snprintf(t->status, sizeof(t->status), "%s", reason);
    else snprintf(t->status, sizeof(t->status), "The connection to your PC was lost (%d).", error);
    t->ended = error == ML_ERROR_GRACEFUL_TERMINATION;
    t->input_ready = false;
    t->state = MOON_FAILED;
}

static void on_log(const char *format, ...)
{
    va_list args;
    va_start(args, format);
    diagnostic_vlog("MOON", format, args);
    va_end(args);
}

static void on_status(int status)
{
    MoonTransport *t = g_transport;
    if (t) t->poor = status == CONN_STATUS_POOR;
}

/* ---- Lifecycle ------------------------------------------------------------- */

bool moon_start(MoonTransport *t, struct _SERVER_INFORMATION *server, struct _STREAM_CONFIGURATION *config)
{
    static DECODER_RENDERER_CALLBACKS video;
    static AUDIO_RENDERER_CALLBACKS audio;
    static CONNECTION_LISTENER_CALLBACKS listener;
    if (!t || g_starting) return false;
    if (g_started) {
        /* Never two connections: the caller closes the old one and retries. */
        t->active = true;
        t->state = MOON_FAILED;
        snprintf(t->status, sizeof(t->status), "The previous stream was still closing. Try again.");
        return false;
    }
    g_starting = true;
    const bool pointer_mode = t->pointer_mode;
    moon_init(t);
    t->pointer_mode = pointer_mode;
    t->active = true;
    t->state = MOON_CHECKING;
    snprintf(t->status, sizeof(t->status), "Connecting to your PC...");
    g_transport = t;
    g_failed_stage = STAGE_NONE;
    g_need_idr = false;

    LiInitializeVideoCallbacks(&video);
    video.setup = video_setup;
    video.submitDecodeUnit = video_submit;
    /* Frames go to MVD's own thread at once; no queue in between. */
    video.capabilities = CAPABILITY_DIRECT_SUBMIT;
    LiInitializeAudioCallbacks(&audio);
    audio.init = audio_init;
    audio.decodeAndPlaySample = audio_sample;
    audio.capabilities = CAPABILITY_DIRECT_SUBMIT | CAPABILITY_SLOW_OPUS_DECODER;
    LiInitializeConnectionCallbacks(&listener);
    listener.stageStarting = on_stage_starting;
    listener.stageFailed = on_stage_failed;
    listener.connectionTerminated = on_terminated;
    listener.logMessage = on_log;
    listener.connectionStatusUpdate = on_status;

    /* On failure moonlight-common-c has already undone its own work. */
    const int rc = LiStartConnection(server, config, &listener, &video, &audio, NULL, 0, NULL, 0);
    if (rc != 0) {
        snprintf(t->status, sizeof(t->status), "Couldn't start the stream (%s, error %d). Check the PC's firewall.",
                 LiGetStageName(g_failed_stage), rc);
        diagnostic_log("MOON", "%s", t->status);
        t->state = MOON_FAILED;
        g_starting = false;
        return false;
    }
    g_started = true;
    t->connected_at = osGetTime();
    t->input_ready = true;
    /* A termination during the last stage must not be overwritten. */
    if (t->state == MOON_CHECKING) {
        snprintf(t->status, sizeof(t->status), "Connected; waiting for video");
        t->state = MOON_CONNECTED;
    }
    g_starting = false;
    return true;
}

void moon_interrupt(void)
{
    if (g_starting) LiInterruptConnection();
}

void moon_close(MoonTransport *t)
{
    if (!t) return;
    /* A connection still being made on the worker: abort it and wait. */
    if (g_starting) LiInterruptConnection();
    while (g_starting) svcSleepThread(10000000LL);
    if (g_started) {
        LiStopConnection();
        g_started = false;
    }
    g_transport = NULL;
    audio_output_close();
    mvd_video_close();
    const bool pointer_mode = t->pointer_mode;
    memset(t, 0, sizeof(*t));
    t->pointer_mode = pointer_mode;
}

bool moon_gameplay_ready(const MoonTransport *t)
{
    return t && t->state == MOON_CONNECTED;
}

unsigned moon_recovered_packets(const MoonTransport *t)
{
    if (!t || t->state != MOON_CONNECTED) return 0;
    const RTP_VIDEO_STATS *stats = LiGetRTPVideoStats();
    return stats ? stats->packetCountFecRecovered : 0;
}

void moon_packet_totals(const MoonTransport *t, unsigned *received, unsigned *recovered, unsigned *failed)
{
    const RTP_VIDEO_STATS *stats = t && t->state == MOON_CONNECTED ? LiGetRTPVideoStats() : NULL;
    *received = stats ? stats->packetCountVideo : 0;
    *recovered = stats ? stats->packetCountFecRecovered : 0;
    *failed = stats ? stats->packetCountFecFailed : 0;
}

/* ---- Input ----------------------------------------------------------------- */

void moon_tick(MoonTransport *t)
{
    if (!t || !t->active) return;
    const uint64_t now = osGetTime();
    const unsigned decoded = mvd_video_decoded_frames();
    if (decoded != t->observed_decoded_frames) {
        t->observed_decoded_frames = decoded;
        t->last_decoded_frame_at = now;
    }
    if (t->state != MOON_CONNECTED || !t->input_ready) return;
    uint32_t rtt = 0, variance = 0;
    if (LiGetEstimatedRttInfo(&rtt, &variance)) {
        t->rtt_ms = (int)rtt;
        t->rtt_variance_ms = (int)variance;
    }

    HostGamepadState state;
    host_input_read_3ds(&state);
    if (t->pointer_mode && !t->keyboard_mode) {
        /* Mouse speed is per 16 ms step, independent of the poll rate. */
        if (now - t->last_mouse_move_at >= 16) {
            const int dx = state.right_x / 2600;
            const int dy = -state.right_y / 2600;
            if ((dx || dy) && LiSendMouseMoveEvent((short)dx, (short)dy) == 0) t->mouse_moves++;
            t->last_mouse_move_at = now;
        }
        state.right_x = state.right_y = 0;
        /* Physical A clicks the mouse; keep B and X out of the game too. */
        state.buttons &= (uint16_t)~host_input_buttons_for_keys(KEY_A | KEY_B | KEY_X);
    }
    if (t->keyboard_mode) {
        /* Physical keyboard shortcuts must not also press gamepad buttons. */
        memset(&state, 0, sizeof(state));
    }
    const bool pressed = !t->input_state_sent ||
        state.buttons != t->last_input_buttons ||
        state.left_trigger != t->last_input_left_trigger ||
        state.right_trigger != t->last_input_right_trigger;
    const bool changed = pressed ||
        state.left_x != t->last_input_left_x || state.left_y != t->last_input_left_y ||
        state.right_x != t->last_input_right_x || state.right_y != t->last_input_right_y;
    if (!changed) return;
    const uint64_t since_last = now - t->last_input_at;
    if (since_last < (pressed ? INPUT_BUTTON_INTERVAL_MS : INPUT_MIN_INTERVAL_MS)) return;

    /* The pad's button bits are XInput's, which Moonlight shares. */
    if (LiSendMultiControllerEvent(0, 1, state.buttons, state.left_trigger, state.right_trigger,
                                   state.left_x, state.left_y, state.right_x, state.right_y) == 0)
        t->input_reports++;
    else
        t->input_failed++;
    t->input_state_sent = true;
    t->last_input_buttons = state.buttons;
    t->last_input_left_trigger = state.left_trigger;
    t->last_input_right_trigger = state.right_trigger;
    t->last_input_left_x = state.left_x; t->last_input_left_y = state.left_y;
    t->last_input_right_x = state.right_x; t->last_input_right_y = state.right_y;
    t->last_input_at = now;
}

void moon_set_pointer_mode(MoonTransport *t, bool enabled)
{
    if (!t || t->pointer_mode == enabled) return;
    t->pointer_mode = enabled;
    /* Switching back to gamepad must also leave the on-screen keyboard mode;
     * that mode suppresses every controller report. */
    if (!enabled) t->keyboard_mode = false;
    diagnostic_log("INPUT", "pointer mode %s", enabled ? "enabled" : "disabled");
}

bool moon_mouse_move(MoonTransport *t, int16_t dx, int16_t dy)
{
    if (!t || !t->input_ready || (!dx && !dy)) return false;
    const bool ok = LiSendMouseMoveEvent(dx, dy) == 0;
    if (ok) t->mouse_moves++;
    return ok;
}

bool moon_mouse_button(MoonTransport *t, bool pressed)
{
    if (!t || !t->input_ready) return false;
    const bool ok = LiSendMouseButtonEvent(pressed ? BUTTON_ACTION_PRESS : BUTTON_ACTION_RELEASE, BUTTON_LEFT) == 0;
    if (ok && !pressed) t->mouse_clicks++;
    return ok;
}

bool moon_send_key(MoonTransport *t, uint16_t keycode, uint16_t scancode, uint16_t modifiers)
{
    (void)scancode;
    if (!t || !t->input_ready) return false;
    /* Moonlight sends Windows virtual keys with the high bit set. */
    const short code = (short)(0x8000 | keycode);
    const bool shift = (modifiers & 1) != 0;
    const char flags = shift ? MODIFIER_SHIFT : 0;
    bool ok = true;
    if (shift) ok &= LiSendKeyboardEvent((short)(0x8000 | VK_LSHIFT_CODE), KEY_ACTION_DOWN, MODIFIER_SHIFT) == 0;
    ok &= LiSendKeyboardEvent(code, KEY_ACTION_DOWN, flags) == 0;
    ok &= LiSendKeyboardEvent(code, KEY_ACTION_UP, flags) == 0;
    if (shift) ok &= LiSendKeyboardEvent((short)(0x8000 | VK_LSHIFT_CODE), KEY_ACTION_UP, 0) == 0;
    if (ok) t->keyboard_keys++;
    /* Never log keys, characters, or login text. */
    return ok;
}
