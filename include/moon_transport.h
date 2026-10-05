#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The stream itself: moonlight-common-c (the Moonlight protocol) carrying
 * H.264 video into the MVD decoder, Opus audio into NDSP, and the gamepad,
 * mouse and keyboard back to the PC. */

struct _SERVER_INFORMATION;
struct _STREAM_CONFIGURATION;

typedef enum {
    MOON_IDLE,
    /* The connection is being set up (RTSP handshake, streams starting). */
    MOON_CHECKING,
    MOON_CONNECTED,
    MOON_FAILED
} MoonState;

typedef struct MoonTransport {
    /* A connection exists or is being made. */
    bool active;
    MoonState state;
    /* The PC ended the stream itself (the app was quit): not a drop. */
    bool ended;
    /* The host says the connection is struggling. */
    bool poor;
    char status[160];

    unsigned video_access_units;
    uint64_t video_bytes;
    uint64_t video_rate_bytes;
    uint64_t video_rate_started_at;
    unsigned video_rate_units;
    unsigned video_kbps;
    /* Frames the PC delivered in the last second. */
    unsigned video_fps;
    /* The PC's capture-to-encode time of the newest frame, in 1/10 ms
     * (0 when the host does not report it). */
    unsigned host_latency;
    uint32_t video_sps_signature;
    unsigned video_source_width;
    unsigned video_source_height;
    unsigned video_source_refs;
    unsigned decoder_width;
    unsigned decoder_height;
    /* A size MVD refused and when: not retried on every frame. */
    unsigned decoder_failed_width, decoder_failed_height;
    uint64_t decoder_failed_at;
    unsigned keyframe_requests;
    unsigned video_idr_units;
    bool video_saw_idr;
    uint64_t last_decoded_frame_at;
    unsigned observed_decoded_frames;

    unsigned audio_packets;
    unsigned audio_decoded;
    unsigned audio_dropped;
    unsigned audio_errors;

    unsigned input_reports;
    /* Reports the control stream refused to take. */
    unsigned input_failed;
    unsigned mouse_moves;
    unsigned mouse_clicks;
    unsigned keyboard_keys;
    bool pointer_mode;
    bool keyboard_mode;
    bool input_ready;
    uint64_t connected_at;
    uint64_t last_input_at;
    uint64_t last_mouse_move_at;
    bool input_state_sent;
    uint16_t last_input_buttons;
    uint8_t last_input_left_trigger;
    uint8_t last_input_right_trigger;
    int16_t last_input_left_x, last_input_left_y;
    int16_t last_input_right_x, last_input_right_y;
    int rtt_ms;
    /* How much the round trip swings, as the control stream estimates it. */
    int rtt_variance_ms;
} MoonTransport;

/* The decoder is fed from the stream's own threads: hold this around
 * presenting a frame. */
void moon_lock(void);
void moon_unlock(void);
/* The stream loop's nap between input polls. */
void moon_wait(MoonTransport *transport, int timeout_ms);
/* HOME Menu or sleep: frames are dropped until it is over. */
void moon_pause(bool paused);

void moon_init(MoonTransport *transport);
/* Network worker: connect to a launched app. Blocks until the stream runs
 * or failed (a few seconds). */
bool moon_start(MoonTransport *transport, struct _SERVER_INFORMATION *server,
                struct _STREAM_CONFIGURATION *config);
/* Any thread: abort a moon_start in progress. */
void moon_interrupt(void);
/* UI thread, every loop: input out, statistics in. */
void moon_tick(MoonTransport *transport);
void moon_close(MoonTransport *transport);
bool moon_gameplay_ready(const MoonTransport *transport);
/* Video packets rebuilt from error correction since the stream started. */
unsigned moon_recovered_packets(const MoonTransport *transport);
/* Video packets since the stream started: received, rebuilt from error
 * correction, and lost beyond rebuilding. Zeros when not connected. */
void moon_packet_totals(const MoonTransport *transport, unsigned *received, unsigned *recovered, unsigned *failed);
void moon_set_pointer_mode(MoonTransport *transport, bool enabled);
bool moon_mouse_move(MoonTransport *transport, int16_t dx, int16_t dy);
bool moon_mouse_button(MoonTransport *transport, bool pressed);
bool moon_send_key(MoonTransport *transport, uint16_t keycode,
                   uint16_t scancode, uint16_t modifiers);
