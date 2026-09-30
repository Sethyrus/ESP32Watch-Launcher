# ESP32Watch-Launcher

Launcher de arranque para la Waveshare **ESP32-S3-Touch-AMOLED-2.06**. Graba en el reloj todos los proyectos ESP32Watch a la vez (Maze, Doom, Fluid) y permite elegir cual abrir, sin recompilar ni reflashear para cambiar de uno a otro.

Stack: `ESP-IDF 5.5.4` + `LVGL 9` + BSP Waveshare + [ESP32Watch-core](https://github.com/Sethyrus/ESP32Watch-core) (`watch_board` >= v0.2.0).

## Como funciona

Cada app sigue siendo su propio firmware, en su propia particion de app (slot OTA). El launcher es la app `factory`.

1. El launcher lista los slots que contienen una app valida: nombre (`project_name` sin el prefijo `ESP32Watch`) y version.
2. Al elegir una, el launcher apunta el siguiente arranque a su slot (`esp_ota_set_boot_partition`, que ademas verifica la imagen) y reinicia.
3. Lo primero que hace la app es `watch_launcher_boot_once()`, que devuelve el siguiente arranque al launcher.

Asi, cualquier reinicio vuelve al launcher: la opcion "Salir" de la app, un cuelgue, "Quit Game" en Doom o un apagado con PWR (6 s). Nunca te quedas atrapado en una app que falla.

Cambiar de app es un reinicio (~1-2 s), asi que cada app arranca limpia: RAM, PSRAM, tareas, DMA, LVGL y display. Lo que deba persistir va en NVS (compartida, un namespace por app) o en la SD.

Controles del launcher:

| Entrada | Accion |
| --- | --- |
| Tocar una app | Abrirla |
| `PWR` corto | Siguiente app |
| `BOOT` | Abrir la seleccionada |

Recuerda la ultima app abierta (NVS, namespace `launcher`).

Salir desde cada app:

| App | Como volver |
| --- | --- |
| Maze | Boton "Salir" en el menu principal |
| Fluid | "Salir al launcher" en el menu de `PWR` (guarda antes los ajustes) |
| Doom | Menu del juego > "Quit Game" |

Esas opciones solo aparecen cuando la app se ha arrancado desde el launcher. Cada app se puede seguir compilando y flasheando sola (`idf.py flash` en su repo); entonces ocupa `factory` en lugar del launcher.

## Particiones

Tabla comun, con los mismos offsets en todos los repos (`partitions.csv` de cada app es una copia de la de aqui):

| Particion | Offset | Tamano | Uso |
| --- | --- | --- | --- |
| `nvs` | `0x9000` | 24 KB | Ajustes de todas las apps |
| `otadata` | `0xf000` | 8 KB | Que app arranca |
| `phy_init` | `0x11000` | 4 KB | |
| `factory` | `0x20000` | 1,5 MB | Launcher |
| `ota_0` | `0x1a0000` | 2 MB | Maze |
| `ota_1` | `0x3a0000` | 2 MB | Doom |
| `ota_2` | `0x5a0000` | 2 MB | Fluid |
| `ota_3` a `ota_6` | `0x7a0000` a `0xda0000` | 2 MB c/u | Libres (apps futuras) |
| `storage` | `0x1000000` | 16 MB | WAD de Doom embebido (FAT, solo lectura); sin uso si Doom lo lee de la SD |

La flash es de 32 MB y esta configurada asi. Todo el codigo (launcher y apps) queda por debajo de los 16 MB porque ejecutar codigo por encima es una funcion experimental de ESP-IDF; por encima solo va `storage`, que se lee por la API de particiones (validado en placa: escritura, lectura y montaje de un FAT). Cada app ocupa hoy ~0,7 MB de sus 2 MB. Cambiar la tabla implica actualizar sus copias en cada app y en `apps.conf`.

## Grabar todo

```sh
source "$HOME/.espressif/v5.5.4/esp-idf/export.sh"
cp .env.example .env    # y ajustar si hace falta
idf.py set-target esp32s3   # solo la primera vez
./flash_all.sh
```

`flash_all.sh` compila el launcher y cada app de `apps.conf`, y graba bootloader, tabla, `otadata` en blanco, launcher, las tres apps y, si Doom lo embebe, la imagen FAT de su WAD. Todo en una sola pasada de esptool. Los logs de compilacion quedan en `build/flash_all_<app>.log`.

| Uso | Que hace |
| --- | --- |
| `./flash_all.sh` | Compila y graba todo |
| `./flash_all.sh fluid` | Compila y regraba solo esa app (un nombre de `apps.conf` o `launcher`), sin tocar las demas |
| `./flash_all.sh --no-build` | Graba lo ya compilado en cada `build/` |

Si falta el repo de una app, o no compila, `flash_all.sh` (modo completo) deja su slot vacio y el launcher no lo muestra. La regrabacion de una sola app supone que el reloj ya tiene esta tabla (un `./flash_all.sh` completo previo). Si el `partitions.csv` de una app no coincide con el de aqui, el script avisa (la grabacion sigue: el reloj usa la tabla del launcher, pero la app en standalone no).

### `.env`

| Variable | Defecto | Uso |
| --- | --- | --- |
| `WATCH_PORT` | vacio (autodetectar) | Puerto serie, p. ej. `/dev/tty.usbmodem1101` |
| `<APP>_DIR` (`MAZE_DIR`, `DOOM_DIR`...) | el de `apps.conf` | Repo de cada app, relativo a este o absoluto |

### WAD de Doom

Lo decide Doom, no el launcher: si su build lo embebe (WAD en `ESP32Watch-Doom/wad/` y `CONFIG_DOOM_EMBED_WAD=y`, ver su `wad/README.md`), deja `build/storage.bin` y `flash_all.sh` lo graba en `storage`. Si no, el script avisa y Doom lee el WAD de la microSD. Un WAD embebido antiguo que quede en `storage` se ignora: Doom solo monta `/internal` cuando se compilo con WAD embebido.

## Anadir una app nueva

El checklist de la app (arranque, "Salir", NVS, tabla) esta en el README de [ESP32Watch-template](https://github.com/Sethyrus/ESP32Watch-template#crear-una-app-nueva-desde-esta-plantilla). Aqui solo:

1. Un slot libre de `partitions.csv` (`ota_3` a `ota_6` estan vacios). Si se acaban, hay que redisenar la tabla sin pasar el codigo de los 16 MB, y copiarla a cada app.
2. Una linea en `apps.conf`: nombre, slot y repo.

El launcher la muestra sola: lista todo slot con una imagen valida. Su color sale de `APP_COLORS` en `components/launcher/launcher.c` (si no esta, usa uno por defecto).

## Documentacion

Hardware, entorno y convenciones: [ESP32Watch-core/docs](https://github.com/Sethyrus/ESP32Watch-core/tree/main/docs).

## Licencia

MIT. Ver [LICENSE](LICENSE).
