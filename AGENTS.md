# AGENTS.md

## Project Shape
- ESP-IDF C firmware `ESP32WatchLauncher`: the boot launcher that runs from `factory` and boots the other ESP32Watch apps (Maze, Doom, Fluid), each flashed into its own OTA slot. `app_main()` in `main/main.c` calls `launcher_start()` in `components/launcher/`.
- Switching apps is a reboot: the launcher calls `esp_ota_set_boot_partition(slot)` + `esp_restart()`, and each app calls `watch_launcher_boot_once()` (core `watch_launcher.h`) first thing, pointing the next boot back at `factory`. Never add state that must survive a switch outside NVS/SD.
- The launcher lists every non-factory app slot with a valid image (`esp_ota_get_partition_description`); names come from `project_name` minus the `ESP32Watch` prefix. Colors per app live in `APP_COLORS` in `launcher.c`.
- Target hardware is Waveshare `ESP32-S3-Touch-AMOLED-2.06` with ESP32-S3R8, AMOLED 410x502 QSPI, FT3168 touch, QMI8658 IMU, PCF85063 RTC, AXP2101 PMU, ES8311 speaker, ES7210 dual-mic ADC, and microSD.
- Baseline stack is `ESP-IDF 5.5.4 + LVGL 9 + waveshare/esp32_s3_touch_amoled_2_06` BSP. Do not migrate to ESP-IDF 6.x or ESP-Brookesia unless explicitly requested.
- Shared board services (`imu_service.h`, `watch_buttons.h`, `watch_launcher.h`) come from `watch_board` in https://github.com/Sethyrus/ESP32Watch-core, pinned by tag in `main/idf_component.yml`. Hardware docs live in that repo's `docs/`.
- Keep `main` small. Add new `main` sources in `main/CMakeLists.txt`, or create ESP-IDF components for reusable code.
- Durable project config lives in `sdkconfig.defaults`, `partitions.csv`, component manifests and `dependencies.lock`. `sdkconfig`, `build/`, and `managed_components/` are generated/local. `.env` is local (template in `.env.example`).
- `partitions.csv` is the shared layout for all app repos: same offsets everywhere, and each app's `partitions.csv` is a copy of it. Changing offsets or slots means updating it here, every app copy, `apps.conf` and the README table. `flash_all.sh` warns when an app copy differs.
- `apps.conf` lists the apps `flash_all.sh` builds and flashes (name, slot, default repo dir; `<NAME>_DIR` in `.env` overrides). Adding an app = a slot + a line there; the app-side checklist lives in the template README.
- NVS is shared with every app: init it with `watch_nvs_init()` and only use the `launcher` namespace.
- Doom owns its WAD choice (`wad/` + `CONFIG_DOOM_EMBED_WAD` in its repo). `flash_all.sh` only flashes Doom's `build/storage.bin` when that build produced it; do not add WAD settings here.

## Commands
- Source ESP-IDF: `source "$HOME/.espressif/v5.5.4/esp-idf/export.sh"`.
- First setup or fresh config: `idf.py set-target esp32s3`.
- Build/primary verification: `idf.py build`.
- Flash launcher + all apps: `./flash_all.sh` (reads `.env`; `./flash_all.sh <app>` reflashes one app, `--no-build` skips builds). It calls `idf.py` through `$IDF_PATH` because some activation scripts define it as a shell function.
- Flash only the launcher and monitor: `idf.py -p <PORT> flash monitor` (macOS port looks like `/dev/tty.usbmodem*` and changes with the USB socket; `idf.py` auto-detects it if `-p` is omitted).
- No repo-local test, lint, or format targets are configured; do not invent npm/PlatformIO/pytest commands.

## Hardware And BSP Notes
- Prefer the Waveshare BSP for display/touch/brightness/audio/SD/I2C. It already handles the QSPI display init, LVGL port and touch input device.
- The wiki mentions display controller `CO5300`, but the ESP-IDF BSP uses `waveshare/esp_lcd_sh8601`. Treat the BSP as source of truth.
- Display brightness is command `0x51` over QSPI, exposed as `bsp_display_brightness_set(percent)`.
- LVGL is not thread-safe. Wrap all `lv_*` calls made outside LVGL callbacks/tasks with `bsp_display_lock()` and `bsp_display_unlock()`.
- Reuse `bsp_i2c_get_handle()` for devices on the shared I2C bus; do not create a second master bus on the same port.
- BOOT is GPIO0, active low. PWR is not a GPIO: it goes to AXP2101 `PWRON` (short press via `watch_pwr_key_take_short_press()`); holding it ~6 s powers off the board.
- Button convention: BOOT = accept/primary action, PWR short press = back/menu. See "Convencion De Botones" in core `docs/ARCHITECTURE.md`.
- For microSD use BSP SDMMC 1-bit (`CLK GPIO2`, `CMD GPIO1`, `D0 GPIO3`). `GPIO17` appears only in Arduino SPI-style SD examples.
- QMI8658 accel is milli-g; `imu_service` already maps axes as `screen_x = -accelY / 1000`, `screen_y = accelX / 1000`.
- Schematic-only pins not wrapped by BSP include motor `GPIO18`, QMI INT `GPIO21`, RTC INT `GPIO39`, LCD TE `GPIO13`, `SYS_OUT/GPIO10`; verify before use.
- For I2C scans, ES7210 appears as `0x40` 7-bit even though `esp_codec_dev` uses an `0x80` default-address macro.
