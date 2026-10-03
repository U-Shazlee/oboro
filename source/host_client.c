#include "host_client.h"

#include <3ds.h>
#include <jansson.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include <Limelight.h>

#include "client.h"
#include "errors.h"
#include "http.h"

#include "app_paths.h"
#include "diagnostic.h"
#include "http_client.h"
#include "moon_transport.h"
#include "stream_profile.h"

#define HOST_PATH APP_DATA_DIR "/host.json"
#define LIBRARY_PATH APP_DATA_DIR "/library.json"
#define KEY_DIR APP_DATA_DIR "/keys"
/* The host holds the pairing request open until the PIN is typed. */
#define PAIR_TIMEOUT_S 120
#define REQUEST_TIMEOUT_S 15

/* libgamestream keeps one host at a time; so does this client. All of it is
 * only touched from the network worker thread. */
static SERVER_DATA g_server;
static STREAM_CONFIGURATION g_config;
static char g_address[64];
static bool g_connected;
/* When the PC last failed to answer: box art must not retry every cover. */
static u64 g_connect_failed_at;

static const char *gs_reason(void)
{
    return gs_error && gs_error[0] ? gs_error : "no answer";
}

static void set_status(HostClient *c, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(c->status, sizeof(c->status), format, args);
    va_end(args);
    diagnostic_log("HOST", "%s", c->status);
}

static void save_host(const HostClient *c)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(APP_DATA_DIR, 0777);
    json_t *root = json_pack("{s:s,s:s}", "address", c->address, "gpu", c->gpu);
    if (!root) return;
    json_dump_file(root, HOST_PATH, JSON_INDENT(2));
    json_decref(root);
}

/* Ask the PC at g_address who it is and whether it knows this console. Also
 * makes this console's key pair on first use, which takes a few seconds. */
static bool connect_address(void)
{
    /* ponytail: one host, reconnected per operation; SERVER_DATA strings
     * from the previous answer are not freed (a few hundred bytes per
     * launch). Keep a host table when multiple PCs are wanted. */
    gs_cleanup();
    g_connected = false;
    memset(&g_server, 0, sizeof(g_server));
    mkdir("sdmc:/3ds", 0777);
    mkdir(APP_DATA_DIR, 0777);
    mkdir(KEY_DIR, 0777);
    gs_http_set_timeout_s(REQUEST_TIMEOUT_S);
    gs_error = NULL;
    g_connected = gs_init(&g_server, g_address, 0, KEY_DIR, 0, true) == GS_OK;
    g_connect_failed_at = g_connected ? 0 : osGetTime();
    return g_connected;
}

static bool connect_host(HostClient *c)
{
    snprintf(g_address, sizeof(g_address), "%s", c->address);
    if (!connect_address()) {
        set_status(c, "Can't reach %.40s (%.60s). Is Sunshine running, on the same Wi-Fi?", c->address, gs_reason());
        return false;
    }
    if (g_server.gpuType) snprintf(c->gpu, sizeof(c->gpu), "%s", g_server.gpuType);
    diagnostic_log("HOST", "connected paired=%d running=%d gpu=%s", g_server.paired, g_server.currentGame, c->gpu);
    return true;
}

/* Connected and still paired; a PC that forgot this console signs it out. */
static bool ensure_paired(HostClient *c)
{
    if (!g_connected && !connect_host(c)) return false;
    if (g_server.paired) return true;
    c->auth_state = HOST_AUTH_LOGGED_OUT;
    set_status(c, "This PC no longer knows this console. Pair again.");
    return false;
}

void host_client_init(HostClient *client)
{
    memset(client, 0, sizeof(*client));
    snprintf(client->status, sizeof(client->status), "Ready");
    json_error_t error;
    json_t *root = json_load_file(HOST_PATH, 0, &error);
    json_t *address = json_is_object(root) ? json_object_get(root, "address") : NULL;
    json_t *gpu = json_is_object(root) ? json_object_get(root, "gpu") : NULL;
    if (json_is_string(address) && json_string_value(address)[0]) {
        snprintf(client->address, sizeof(client->address), "%s", json_string_value(address));
        snprintf(g_address, sizeof(g_address), "%s", client->address);
        if (json_is_string(gpu)) snprintf(client->gpu, sizeof(client->gpu), "%s", json_string_value(gpu));
        client->auth_state = HOST_AUTH_LOGGED_IN;
    }
    json_decref(root);
}

bool host_begin_login(HostClient *c, const char *address)
{
    /* The address comes from the on-screen keyboard: keep what a host name
     * or IP address can hold and nothing else (it goes into URLs). */
    size_t n = 0;
    for (const char *p = address; p && *p && n + 1 < sizeof(c->address); ++p)
        if ((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
            *p == '.' || *p == '-')
            c->address[n++] = *p;
    c->address[n] = '\0';
    c->pair_pending = false;
    if (!n) {
        c->auth_state = HOST_AUTH_LOGGED_OUT;
        set_status(c, "Enter your PC's IP address, like 192.168.1.20");
        return false;
    }
    if (!connect_host(c)) {
        c->auth_state = HOST_AUTH_LOGGED_OUT;
        return false;
    }
    if (g_server.paired) {
        c->auth_state = HOST_AUTH_LOGGED_IN;
        save_host(c);
        set_status(c, "Paired with %.60s", c->address);
        return host_fetch_library(c);
    }
    srand((unsigned)(svcGetSystemTick() ^ osGetTime()));
    snprintf(c->user_code, sizeof(c->user_code), "%04d", rand() % 10000);
    snprintf(c->verification_uri, sizeof(c->verification_uri), "https://%s:47990/pin", c->address);
    c->challenge_expires_at = (int64_t)time(NULL) + PAIR_TIMEOUT_S;
    c->pair_pending = true;
    c->auth_state = HOST_AUTH_WAITING;
    set_status(c, "Type the PIN on your PC");
    return true;
}

void host_tick(HostClient *c)
{
    if (c->auth_state != HOST_AUTH_WAITING || !c->pair_pending) return;
    c->pair_pending = false;
    gs_http_set_timeout_s(PAIR_TIMEOUT_S);
    gs_error = NULL;
    const int rc = gs_pair(&g_server, c->user_code);
    gs_http_set_timeout_s(REQUEST_TIMEOUT_S);
    c->user_code[0] = '\0';
    if (rc != GS_OK) {
        c->auth_state = HOST_AUTH_LOGGED_OUT;
        set_status(c, "Pairing failed (%.80s). Try again.", gs_reason());
        return;
    }
    c->auth_state = HOST_AUTH_LOGGED_IN;
    save_host(c);
    set_status(c, "Paired with %.60s", c->address);
    host_fetch_library(c);
}

static void save_library(const HostClient *c)
{
    json_t *apps = json_array();
    for (size_t i = 0; apps && i < c->game_count; ++i)
        json_array_append_new(apps, json_pack("{s:s,s:s,s:s}", "title", c->games[i].title, "id", c->games[i].app_id,
                                              "source", c->games[i].store));
    json_t *root = apps ? json_pack("{s:I,s:o}", "saved_at", (json_int_t)c->library_saved_at, "apps", apps) : NULL;
    if (!root) return;
    json_dump_file(root, LIBRARY_PATH, JSON_COMPACT);
    json_decref(root);
}

/* Ids go into URLs and file names: letters, digits and dashes only. */
static bool valid_id(const char *id)
{
    if (!id || !id[0] || strlen(id) >= sizeof(((HostGame *)0)->app_id)) return false;
    for (const char *p = id; *p; ++p)
        if (!((*p >= '0' && *p <= '9') || (*p >= 'a' && *p <= 'z') || *p == '-')) return false;
    return true;
}

/* Sunshine's own apps have plain numbers; Oboro Host's games do not. */
static bool from_oboro_host(const char *id)
{
    return id[strspn(id, "0123456789")] != '\0';
}

/* One request to Oboro Host on the PC; true on HTTP 200 (free the response
 * either way). The header keeps web pages from starting games. */
static bool oboro_host_request(const char *method, const char *path, size_t limit, HttpResponse *response)
{
    static const char *const headers[] = { "X-Oboro: 1" };
    char url[192];
    snprintf(url, sizeof(url), "http://%.64s:%d%s", g_address, OBORO_HOST_PORT, path);
    http_next_request(5, NULL, NULL);
    return http_request(method, url, APP_NAME "-3DS", headers, 1, NULL, limit, response) &&
           response->status == 200;
}

static void add_game(HostClient *c, const char *title, const char *id, const char *source)
{
    if (c->game_count >= HOST_MAX_GAMES || !title || !title[0] || !valid_id(id)) return;
    HostGame *game = &c->games[c->game_count++];
    memset(game, 0, sizeof(*game));
    snprintf(game->title, sizeof(game->title), "%s", title);
    snprintf(game->app_id, sizeof(game->app_id), "%s", id);
    snprintf(game->store, sizeof(game->store), "%s", source && source[0] ? source : "PC");
    snprintf(game->image_url, sizeof(game->image_url), "appasset");
}

/* The library Oboro Host offers: Steam and custom games. False when it is
 * not running on the PC (or sent nothing usable). */
static bool fetch_oboro_library(HostClient *c)
{
    HttpResponse response;
    const bool ok = oboro_host_request("GET", "/library", 256 * 1024, &response);
    json_error_t error;
    json_t *root = ok && response.body ? json_loadb(response.body, response.size, 0, &error) : NULL;
    http_response_free(&response);
    json_t *games = json_is_object(root) ? json_object_get(root, "games") : NULL;
    if (!json_is_array(games) || !json_array_size(games)) {
        json_decref(root);
        return false;
    }
    c->game_count = 0;
    size_t index;
    json_t *game;
    json_array_foreach(games, index, game) {
        json_t *title = json_object_get(game, "title"), *id = json_object_get(game, "id");
        json_t *source = json_object_get(game, "source");
        if (json_is_string(title) && json_is_string(id))
            add_game(c, json_string_value(title), json_string_value(id),
                     json_is_string(source) ? json_string_value(source) : NULL);
    }
    json_decref(root);
    return c->game_count > 0;
}

/* Sunshine's app to stream while Oboro Host runs the game: "Desktop", or
 * failing that the first app. 0 when Sunshine has none. */
static int desktop_app_id(void)
{
    PAPP_LIST list = NULL;
    int found = 0, first = 0;
    if (gs_applist(&g_server, &list) != GS_OK) return 0;
    while (list) {
        PAPP_LIST next = list->next;
        if (!first) first = list->id;
        if (list->name && !strcasecmp(list->name, "Desktop")) found = list->id;
        free(list->name);
        free(list);
        list = next;
    }
    return found ? found : first;
}

bool host_fetch_library(HostClient *c)
{
    if (!ensure_paired(c)) return false;
    if (fetch_oboro_library(c)) {
        c->library_saved_at = (int64_t)time(NULL);
        save_library(c);
        set_status(c, "%u games on your PC", (unsigned)c->game_count);
        return true;
    }
    /* Without Oboro Host there is still Sunshine's own list. */
    PAPP_LIST list = NULL;
    gs_error = NULL;
    if (gs_applist(&g_server, &list) != GS_OK) {
        set_status(c, "Couldn't load the app list (%.80s)", gs_reason());
        return false;
    }
    c->game_count = 0;
    while (list) {
        PAPP_LIST next = list->next;
        char id[16];
        snprintf(id, sizeof(id), "%d", list->id);
        add_game(c, list->name, id, "Sunshine");
        free(list->name);
        free(list);
        list = next;
    }
    c->library_saved_at = (int64_t)time(NULL);
    save_library(c);
    set_status(c, "Oboro Host isn't running on your PC: showing Sunshine's %u apps", (unsigned)c->game_count);
    return true;
}

bool host_library_load(HostClient *c)
{
    json_error_t error;
    json_t *root = json_load_file(LIBRARY_PATH, 0, &error);
    json_t *apps = json_is_object(root) ? json_object_get(root, "apps") : NULL;
    if (!json_is_array(apps)) {
        json_decref(root);
        return false;
    }
    c->game_count = 0;
    size_t index;
    json_t *app;
    json_array_foreach(apps, index, app) {
        json_t *title = json_object_get(app, "title"), *id = json_object_get(app, "id");
        json_t *source = json_object_get(app, "source");
        if (json_is_string(title) && json_is_string(id))
            add_game(c, json_string_value(title), json_string_value(id),
                     json_is_string(source) ? json_string_value(source) : NULL);
    }
    json_t *saved = json_object_get(root, "saved_at");
    c->library_saved_at = json_is_integer(saved) ? (int64_t)json_integer_value(saved) : 0;
    json_decref(root);
    return c->game_count > 0;
}

bool host_start_session(HostClient *c, const HostGame *game, bool launch)
{
    c->conflict_found = false;
    c->fail_code[0] = '\0';
    c->session_state = HOST_SESSION_SETUP;
    /* Always ask again: what runs on the PC decides launch or resume. */
    g_connected = false;
    if (!ensure_paired(c)) {
        c->session_state = HOST_SESSION_IDLE;
        snprintf(c->fail_code, sizeof(c->fail_code), "network");
        return false;
    }
    /* A game from Oboro Host plays on the streamed desktop. */
    const bool hosted = from_oboro_host(game->app_id);
    const int app_id = hosted ? desktop_app_id() : atoi(game->app_id);
    if (!app_id) {
        c->session_state = HOST_SESSION_IDLE;
        snprintf(c->fail_code, sizeof(c->fail_code), "nodesktop");
        set_status(c, "Sunshine has no \"Desktop\" app to stream. Add one in Sunshine > Applications.");
        return false;
    }
    if (g_server.currentGame && g_server.currentGame != app_id) {
        c->conflict_found = true;
        snprintf(c->conflict.app_id, sizeof(c->conflict.app_id), "%d", g_server.currentGame);
        c->session_state = HOST_SESSION_IDLE;
        snprintf(c->fail_code, sizeof(c->fail_code), "busy");
        set_status(c, "Another app is already running on your PC.");
        return false;
    }
    LiInitializeStreamConfiguration(&g_config);
    g_config.width = (int)stream_profile_width();
    g_config.height = (int)stream_profile_height();
    g_config.fps = 30;
    g_config.bitrate = (int)stream_profile_initial_bitrate();
    g_config.packetSize = 1392;
    g_config.streamingRemotely = STREAM_CFG_AUTO;
    g_config.audioConfiguration = AUDIO_CONFIGURATION_STEREO;
    g_config.supportedVideoFormats = VIDEO_FORMAT_H264;
    g_config.encryptionFlags = ENCFLG_NONE;
    gs_error = NULL;
    const int rc = gs_start_app(&g_server, &g_config, app_id, false, false, 1);
    if (rc != GS_OK) {
        c->session_state = HOST_SESSION_IDLE;
        snprintf(c->fail_code, sizeof(c->fail_code), "launch");
        set_status(c, "Your PC couldn't start %.60s (%.60s)", game->title, gs_reason());
        return false;
    }
    /* The stream session exists: now the game itself, on the PC. */
    if (hosted && launch && strcmp(game->app_id, "desktop")) {
        char path[96];
        snprintf(path, sizeof(path), "/launch?id=%s", game->app_id);
        HttpResponse response;
        const bool started = oboro_host_request("POST", path, 4096, &response);
        const long status = response.status;
        http_response_free(&response);
        if (!started) {
            c->session_state = HOST_SESSION_IDLE;
            snprintf(c->fail_code, sizeof(c->fail_code), "host");
            set_status(c, status == 404 ? "%.60s is no longer in the library on your PC. Press Y in the library to refresh."
                                        : "Oboro Host on your PC didn't answer, so %.60s wasn't started.", game->title);
            return false;
        }
    }
    c->session_state = HOST_SESSION_READY;
    set_status(c, "Starting the stream...");
    return true;
}

bool host_stream_start(HostClient *c, struct MoonTransport *transport)
{
    if (c->session_state != HOST_SESSION_READY) return false;
    return moon_start(transport, &g_server.serverInfo, &g_config);
}

bool host_stop_session(HostClient *c)
{
    /* The app is left running on the PC, as Moonlight does: quitting it
     * from here would kill a game that has not saved. */
    c->session_state = HOST_SESSION_IDLE;
    set_status(c, "Disconnected");
    return true;
}

bool host_session_active(const HostClient *c) { return c->session_state != HOST_SESSION_IDLE; }

bool host_end_conflict(HostClient *c, const HostGame *game)
{
    if (!ensure_paired(c)) return false;
    gs_error = NULL;
    if (gs_quit_app(&g_server) != GS_OK) {
        snprintf(c->fail_code, sizeof(c->fail_code), "quit");
        set_status(c, "Couldn't quit the running app (%.80s). Another device may have started it.", gs_reason());
        return false;
    }
    return host_start_session(c, game, true);
}

bool host_has_session(const HostClient *c) { return c->auth_state == HOST_AUTH_LOGGED_IN; }

void host_sign_out(HostClient *c)
{
    if (g_connected || connect_host(c)) gs_unpair(&g_server);
    gs_cleanup();
    g_connected = false;
    remove(HOST_PATH);
    remove(LIBRARY_PATH);
    memset(c, 0, sizeof(*c));
    snprintf(c->status, sizeof(c->status), "PC forgotten");
}

bool host_fetch_art(const char *app_id, char **image, size_t *size)
{
    if (!valid_id(app_id)) return false;
    if (from_oboro_host(app_id)) {
        HttpResponse response = {0};
        char path[96];
        snprintf(path, sizeof(path), "/art?id=%s", app_id);
        if (!g_address[0] || !oboro_host_request("GET", path, 1024 * 1024, &response) || !response.body) {
            http_response_free(&response);
            return false;
        }
        /* Hand the buffer over instead of copying it. */
        *image = response.body;
        *size = response.size;
        return true;
    }
    /* The library can be on screen from the SD card before anything spoke
     * to the PC. A PC that is off is asked again only after a minute. */
    if (!g_connected) {
        if (!g_address[0] || (g_connect_failed_at && osGetTime() - g_connect_failed_at < 60000)) return false;
        if (!connect_address()) return false;
    }
    if (!g_server.paired) return false;
    return gs_app_asset(&g_server, atoi(app_id), image, size) == GS_OK;
}

void host_cancel(void)
{
    gs_http_cancel();
    moon_interrupt();
}
