#pragma once

#define APP_NAME "Oboro"
#define APP_BUILD "1"
/* Set by the Makefile from VERSION_MAJOR / MINOR / MICRO / SUFFIX. */
#ifndef APP_VERSION
#define APP_VERSION "0.0.0-dev"
#endif
/* GitHub repository that releases (and updates) come from, "owner/name".
 * While it contains "CHANGE-ME" the updater is switched off. */
#define APP_REPOSITORY "CHANGE-ME/oboro"
#define APP_DATA_DIR "sdmc:/3ds/oboro"
/* Opt-in diagnostic reports go here (Kasumi's server/report-worker can be
 * deployed for it). A URL containing "CHANGE-ME" disables the feature: no
 * report or statistic is ever sent, and the settings for it are hidden. */
#define REPORT_BASE "https://CHANGE-ME.invalid"
#define REPORT_URL REPORT_BASE "/report"
#define STATS_URL REPORT_BASE "/stats"
#define LAUNCHES_URL REPORT_BASE "/launches"

/* Create the data folder on the SD card. */
void app_paths_migrate(void);
