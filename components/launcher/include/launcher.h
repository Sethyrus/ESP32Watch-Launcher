#pragma once

#include "esp_err.h"

// Lists the apps found in the OTA slots and boots the chosen one (see README).
esp_err_t launcher_start(void);
