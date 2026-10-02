# AGENTS.md

## Project Shape
- ESP-IDF C firmware `ESP32WatchLauncher`: the watch "OS" that runs from `factory`: watch face, menu of watch features, settings, and an Apps section that boots the other ESP32Watch apps (Maze, Doom, Fluid, Recorder), each flashed into its own OTA slot. `app_main()` in `main/main.c` calls `launcher_start()` in `components/launcher/`.
- UI structure: `os_ui.c` owns the screen stack (`os_push`/`os_back`/`os_home`, screens rebuilt on every change, old one deleted async), button focus (BOOT long = next, BOOT short = click) and the system task (buttons, dim 3 s then sleep via `watch_power_sleep`, 1 s ticks). One screen per file (`os_face.c`, `os_menu.c`, `os_apps.c`, `os_settings.c`, `os_tools.c`, `os_timer.c`, `os_alarms.c`, `os_usb.c`); timer/alarms model, NVS and sound in `os_alerts.c` (absolute wall-clock instants; the system task polls `os_alerts_poll` and sleeps with `watch_power_sleep(next due)`; the alert screen always sits over the face at depth 2); theme, fonts and icon glyphs in `os.h`; NVS in `os_store.c`. The system task holds the LVGL lock while it calls into screens.
- USB mode (`os_usb.c`, Ajustes > Conectar al ordenador): TinyUSB mass storage (`espressif/esp_tinyusb` 2.x) over the microSD, initialised on the BSP SDMMC pins without mounting FAT locally. It takes the only USB port from USB-Serial-JTAG; leaving reboots, and `os_usb_restore_port()` (also called at boot) sets the RTC-domain PHY mux back, because a software reset keeps it on USB-OTG and the console never comes back.
- Wake only with BOOT or PWR, never touch (the owner's choice). Keep watch features in the Menu, apart from Apps and Settings.
- Fonts in `components/launcher/fonts/` are generated with `lv_font_conv` (Barlow, Barlow Condensed, Lucide) with only the glyphs used; add a glyph by regenerating the file.
- Switching apps is a reboot: the launcher calls `esp_ota_set_boot_partition(slot)` + `esp_restart()`, and each app calls `watch_launcher_boot_once()` (core `watch_launcher.h`) first thing, pointing the next boot back at `factory`. Never add state that must survive a switch outside NVS/SD.
- The launcher lists every non-factory app slot with a valid image (`esp_ota_get_partition_description`); names come from `project_name` minus the `ESP32Watch` prefix. Colour, icon and description per known app live in `KNOWN` in `os_apps.c`.
- Target hardware is Waveshare `ESP32-S3-Touch-AMOLED-2.06` with ESP32-S3R8, AMOLED 410x502 QSPI, FT3168 touch, QMI8658 IMU, PCF85063 RTC, AXP2101 PMU, ES8311 speaker, ES7210 dual-mic ADC, and microSD.
- Baseline stack is `ESP-IDF 5.5.4 + LVGL 9 + waveshare/esp32_s3_touch_amoled_2_06` BSP. Do not migrate to ESP-IDF 6.x or ESP-Brookesia unless explicitly requested.
- Shared board services (`watch_display.h`, `watch_power.h`, `watch_buttons.h`, `watch_battery.h`, `watch_rtc.h`, `watch_nvs.h`) come from `watch_board` in https://github.com/Sethyrus/ESP32Watch-core, pinned by tag in `main/idf_component.yml` (v0.5.1). Sleep goes through `watch_power_sleep()`, which also turns the touch off while asleep (core 0.5.1; before it the watch could reboot on wake). Hardware docs live in that repo's `docs/`.
- Keep `main` small. Add new `main` sources in `main/CMakeLists.txt`, or create ESP-IDF components for reusable code.
- Durable project config lives in `sdkconfig.defaults`, `partitions.csv`, component manifests and `dependencies.lock`. `sdkconfig`, `build/`, and `managed_components/` are generated/local. `.env` is local (template in `.env.example`).
- `partitions.csv` is the shared layout for all app repos: same offsets everywhere, and each app's `partitions.csv` is a copy of it. Changing offsets or slots means updating it here, every app copy, `apps.conf` and the README table. `flash_all.sh` warns when an app copy differs.
- `apps.conf` is the catalogue of apps (name, fixed slot, default repo dir); `APPS` in `.env` picks which go on the watch (empty = all), and a full flash empties the slots of the rest. It lists the apps `flash_all.sh` builds and flashes (name, slot, default repo dir; `<NAME>_DIR` in `.env` overrides). Adding an app = a slot + a line there; the app-side checklist lives in the template README.
- NVS is shared with every app: init it with `watch_nvs_init()` and only use the `launcher` namespace.
- Doom owns its WAD choice (`wad/` + `CONFIG_DOOM_EMBED_WAD` in its repo). `flash_all.sh` only flashes Doom's `build/storage.bin` when that build produced it; do not add WAD settings here.

## Commands
- Source ESP-IDF: `source "$HOME/.espressif/tools/activate_idf_v5.5.4.sh"` (EIM install; otherwise core `docs/SETUP.md`).
- First setup or fresh config: `idf.py set-target esp32s3`.
- Build/primary verification: `idf.py build`.
- Flash launcher + all apps: `./flash_all.sh` (reads `.env`; `./flash_all.sh <app>` reflashes one app, `--no-build` skips builds). It calls `idf.py` through `$IDF_PATH` because some activation scripts define it as a shell function.
- Flash only the launcher and monitor: `idf.py -p <PORT> flash monitor` (macOS port looks like `/dev/tty.usbmodem*` and changes with the USB socket; `idf.py` auto-detects it if `-p` is omitted).
- No repo-local test, lint, or format targets are configured; do not invent npm/PlatformIO/pytest commands.

## Hardware And BSP Notes
- Display, touch and LVGL: `watch_display_start()` from core, never `bsp_display_start()` (it registers the QSPI panel as RGB: heap overrun). Prefer the Waveshare BSP for audio, SD and I2C.
- The wiki mentions display controller `CO5300`, but the ESP-IDF BSP uses `waveshare/esp_lcd_sh8601`. Treat the BSP as source of truth.
- Display brightness is command `0x51` over QSPI, exposed as `bsp_display_brightness_set(percent)`.
- LVGL is not thread-safe. Wrap all `lv_*` calls made outside LVGL callbacks/tasks with `bsp_display_lock()` and `bsp_display_unlock()`.
- Reuse `bsp_i2c_get_handle()` for devices on the shared I2C bus; do not create a second master bus on the same port.
- BOOT is GPIO0, active low. PWR is not a GPIO: it goes to AXP2101 `PWRON` (short press via `watch_pwr_key_take_short_press()`); holding it ~6 s powers off the board.
- Button convention: BOOT = accept/primary action, PWR short press = back/menu. See "Convencion De Botones" in core `docs/ARCHITECTURE.md`.
- Never write AXP2101 power or protection registers; read core `docs/PMU_SAFETY.md` before any PMU access. Core fixes reach this app by bumping the `watch_board` tag (core `AGENTS.md`, "Alineacion De Repos").
- For microSD use BSP SDMMC 1-bit (`CLK GPIO2`, `CMD GPIO1`, `D0 GPIO3`). `GPIO17` appears only in Arduino SPI-style SD examples.
- QMI8658 accel is milli-g; `imu_service` already maps axes as `screen_x = -accelY / 1000`, `screen_y = accelX / 1000`.
- There is no vibration motor (`GPIO18` does nothing; core `docs/BRINGUP.md`). Schematic-only pins not wrapped by BSP include QMI INT `GPIO21`, RTC INT `GPIO39`, LCD TE `GPIO13`, `SYS_OUT/GPIO10`; verify before use.
- For I2C scans, ES7210 appears as `0x40` 7-bit even though `esp_codec_dev` uses an `0x80` default-address macro.
