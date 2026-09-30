#pragma once

#include "esp_err.h"

// Starts the watch OS: face, menu, watch tools, settings and the Apps list, which
// boots the chosen app from its OTA slot (see README).
esp_err_t launcher_start(void);
