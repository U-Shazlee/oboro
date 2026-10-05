#include "pc_stats.h"

#include <3ds.h>
#include <jansson.h>
#include <stdio.h>

#include "app_paths.h"
#include "host_client.h"
#include "http_client.h"

#define POLL_MS 2000
/* Without the companion script the port is closed: ask rarely. */
#define RETRY_MS 15000
#define STALE_MS 6000

/* Written by the worker, read by the UI: plain numbers, so a torn read
 * shows one tile a poll late at worst. */
static PcStats g_stats;
static u64 g_updated_at, g_next_poll_at;

static int number(json_t *root, const char *key)
{
    json_t *value = json_object_get(root, key);
    return json_is_number(value) ? (int)(json_number_value(value) + 0.5) : -1;
}

void pc_stats_poll(const char *address, const char *key)
{
    const u64 now = osGetTime();
    if (!address || !address[0] || now < g_next_poll_at) return;
    char url[128];
    snprintf(url, sizeof(url), "http://%.64s:%d/stats", address, OBORO_HOST_PORT);
    char key_header[40];
    snprintf(key_header, sizeof(key_header), "X-Oboro-Key: %.20s", key && key[0] ? key : "none");
    const char *const headers[] = { key_header };
    HttpResponse response;
    http_next_request(2, NULL, NULL);
    const bool ok = http_request("GET", url, APP_NAME "-3DS", headers, 1, NULL, 4096, &response) &&
                    response.status == 200 && response.body;
    json_error_t error;
    json_t *root = ok ? json_loadb(response.body, response.size, 0, &error) : NULL;
    http_response_free(&response);
    const bool answered = json_is_object(root);
    if (answered) {
        g_stats.cpu = number(root, "cpu");
        g_stats.ram = number(root, "ram");
        g_stats.gpu = number(root, "gpu");
        g_stats.vram = number(root, "vram");
        g_stats.gpu_temp = number(root, "gpu_temp");
        g_stats.fps = number(root, "fps");
        g_updated_at = osGetTime();
    }
    json_decref(root);
    g_next_poll_at = osGetTime() + (answered ? POLL_MS : RETRY_MS);
}

PcStats pc_stats_get(void)
{
    PcStats stats = g_stats;
    const u64 now = osGetTime();
    stats.valid = g_updated_at && now - g_updated_at < STALE_MS;
    return stats;
}
