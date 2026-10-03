#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The gaming PC this console streams from: a Sunshine (or GameStream) host
 * on the local network. Pairing and starting the stream go through
 * libgamestream; the stream itself is moon_transport.
 *
 * The library comes from Oboro Host (host/oboro_host.py on the PC): the
 * installed Steam games plus the custom games added there. Such a game
 * streams Sunshine's "Desktop" while Oboro Host starts it on the PC. When
 * Oboro Host is not running, Sunshine's own app list is shown instead. */

/* Oboro Host's port on the PC: library, launching, cover art, PC stats. */
#define OBORO_HOST_PORT 48100

#define HOST_MAX_GAMES 256
#define HOST_MAX_VARIANTS 4

typedef enum {
    HOST_AUTH_LOGGED_OUT,
    /* The PIN is on screen, waiting for it to be typed on the PC. */
    HOST_AUTH_WAITING,
    HOST_AUTH_LOGGED_IN,
    HOST_AUTH_ERROR
} HostAuthState;

typedef enum {
    HOST_SESSION_IDLE,
    HOST_SESSION_SETUP,
    HOST_SESSION_READY,
    HOST_SESSION_ERROR
} HostSessionState;

typedef struct {
    char title[96];
    char app_id[48];
    char store[24];
    /* Non-empty when the host can be asked for box art. */
    char image_url[192];
    /* Kept for the details page; a PC app has one version. */
    struct {
        char id[16];
        char store[16];
    } variants[HOST_MAX_VARIANTS];
    unsigned variant_count;
    unsigned variant_selected;
} HostGame;

/* An app already running on the PC, in the way of a launch. */
typedef struct {
    char app_id[24];
} HostConflict;

typedef struct {
    HostAuthState auth_state;
    char status[160];
    /* The PC's address as typed ("192.168.1.20"). */
    char address[64];
    /* The PC's graphics card, as it reports it. */
    char gpu[64];
    /* Pairing: the PIN to type on the PC and where to type it. */
    char user_code[32];
    char verification_uri[256];
    int64_t challenge_expires_at;
    /* The PIN is shown; the worker still has to send the pairing request. */
    bool pair_pending;
    HostGame games[HOST_MAX_GAMES];
    size_t game_count;
    /* When the app list shown was fetched (0 = never / not cached). */
    int64_t library_saved_at;
    HostSessionState session_state;
    /* A launch refused because another app runs on the PC: the UI asks
     * whether to quit it (host_end_conflict). */
    bool conflict_found;
    HostConflict conflict;
    /* Why the last launch failed, short and fixed ("network", "busy"...). */
    char fail_code[16];
} HostClient;

struct MoonTransport;

void host_client_init(HostClient *client);
/* Reach the PC at `address`; paired already, or a PIN to pair with. */
bool host_begin_login(HostClient *client, const char *address);
/* Worker, when idle: sends the pairing request once the PIN is shown. */
void host_tick(HostClient *client);
bool host_fetch_library(HostClient *client);
/* The app list as last fetched, from the SD card (instant, offline). */
bool host_library_load(HostClient *client);
/* Open a stream session for `game` on the PC; the stream follows once
 * READY. `launch`: also start the game (false when reconnecting to one that
 * is already running). */
bool host_start_session(HostClient *client, const HostGame *game, bool launch);
/* Start the stream of a READY session on `transport` (blocks a few seconds). */
bool host_stream_start(HostClient *client, struct MoonTransport *transport);
/* Leave the stream. The app keeps running on the PC and can be resumed. */
bool host_stop_session(HostClient *client);
bool host_session_active(const HostClient *client);
/* Quit the app in the way, then launch `game`. */
bool host_end_conflict(HostClient *client, const HostGame *game);
/* Paired with a PC. */
bool host_has_session(const HostClient *client);
/* Unpair and forget the PC and its app list. */
void host_sign_out(HostClient *client);
/* Worker thread: one app's box art as the PC serves it (PNG); free *image. */
bool host_fetch_art(const char *app_id, char **image, size_t *size);
/* Abort the request in flight (from any thread). */
void host_cancel(void);
