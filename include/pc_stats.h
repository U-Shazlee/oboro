#pragma once

#include <stdbool.h>

/* What the gaming PC is doing while it streams: CPU, GPU, memory and the
 * game's own frame rate. The Moonlight protocol carries none of this (a PC
 * can only be measured on the PC), so it comes from Oboro Host, the same
 * program that serves the library. Without it the PC tiles show "-". */

typedef struct {
    /* An answer arrived in the last few seconds. */
    bool valid;
    /* Percentages 0-100; -1 when the PC could not measure that value. */
    int cpu, ram, gpu, vram;
    /* Degrees Celsius, -1 unknown. */
    int gpu_temp;
    /* The game's render rate on the PC (needs RivaTuner on the PC), -1 unknown. */
    int fps;
} PcStats;

/* Network worker, while a stream runs: asks the PC at most every 2 s. */
void pc_stats_poll(const char *address);
/* UI thread: the newest answer. */
PcStats pc_stats_get(void);
