#include "app_paths.h"

#include <sys/stat.h>

void app_paths_migrate(void)
{
    mkdir("sdmc:/3ds", 0777);
    mkdir(APP_DATA_DIR, 0777);
}
