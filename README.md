# ESP32Watch-Launcher

El "sistema" del reloj para la Waveshare **ESP32-S3-Touch-AMOLED-2.06**: esfera con la hora, menu con las funciones del reloj, ajustes y una seccion Apps desde la que se abren los demas proyectos ESP32Watch (Maze, Doom, Fluid, Recorder), grabados todos a la vez, sin recompilar ni reflashear para cambiar de uno a otro.

Stack: `ESP-IDF 5.5.4` + `LVGL 9` + BSP Waveshare + [ESP32Watch-core](https://github.com/Sethyrus/ESP32Watch-core) (`watch_board` v0.5.1).

## El reloj

Diseno: fondo negro (en AMOLED el negro no consume), texto blanco calido, un acento turquesa y ambar para bateria baja. Hora en Barlow Condensed, texto en Barlow, iconos de Lucide (fuentes LVGL en `components/launcher/fonts/`, con sus licencias OFL e ISC).

| Pantalla | Contenido |
| --- | --- |
| Esfera | Bateria por tramos, fecha, hora grande, barra de segundos, cronometro si esta en marcha y botones a Apps y Ajustes |
| Menu | Apps, Cronometro, Temporizador, Alarmas, Linterna y Ajustes |
| Temporizador | Minutos y segundos con rodillos; cuenta atras con pausa y reinicio |
| Alarmas | 4 alarmas (hora, una vez o cada dia); ON/OFF desde la lista |
| Apps | Las apps grabadas; abrir una reinicia en ella |
| Ajustes | Hora y fecha, brillo (5 niveles), apagado de pantalla (10/15/30/60 s), bateria, conectar al ordenador, acerca de, apagar el reloj |

| Entrada | Accion |
| --- | --- |
| `BOOT` en la esfera | Abrir el menu (tambien deslizando hacia arriba) |
| `PWR` en la esfera | Apagar la pantalla |
| `BOOT` corto | Abrir o aceptar lo marcado con el aro turquesa |
| `BOOT` mantenido | Pasar el aro al siguiente elemento |
| `PWR` corto | Volver |
| Tactil | Todo, con la pantalla encendida |

Sin uso durante el tiempo de Ajustes, la pantalla se oscurece 3 s y se apaga; tocar o pulsar un boton mientras esta oscurecida solo la reactiva. Apagada, **solo la despiertan BOOT o PWR** (el tactil no, para que no se encienda sola) y vuelve a la esfera. En bateria el chip entra en light sleep; con USB conectado se queda despierto con la pantalla apagada, porque el USB-Serial-JTAG no funciona en light sleep. Si el RTC perdio la hora, arranca en "Hora y fecha".

Temporizador y alarmas guardan en NVS el instante absoluto en que suenan, asi que siguen vigentes con una app abierta y con el reloj dormido: el sueno dura solo hasta la proxima (`watch_power_sleep(timeout)`), enciende la pantalla y suena (dos pitidos por el altavoz, hasta 60 s; la placa no tiene motor de vibracion; BOOT, PWR o el boton lo paran). Si vencieron hace mas de 2 min (p. ej. jugando a Doom) se muestran como "perdida", sin sonido. La esfera muestra la proxima alarma y el temporizador en marcha. Sin posponer todavia.

**Conectar al ordenador** (Ajustes): la microSD aparece como disco USB en el ordenador (TinyUSB, clase de almacenamiento), con todo lo que tenga: grabaciones de la Recorder, partidas de Doom... El chip tiene un solo USB, que normalmente usa la consola (USB-Serial-JTAG); mientras dura, no hay consola ni grabacion de firmware por USB, y la pantalla no se apaga (dormir cortaria la conexion). Salir (boton, BOOT o PWR) reinicia el reloj para devolver el puerto a la consola: hay que expulsar el disco antes en el ordenador. Si una alarma vence con este modo activo, la pantalla de alarma apagaria la pantalla y cortaria la conexion (sin probar): no uses el modo USB con una alarma inminente. Codigo en `components/launcher/os_usb.c`.

Al arrancar apaga el IMU y el amplificador, que una app puede haber dejado encendidos (`esp_restart()` no los resetea). Ajustes y cronometro se guardan en NVS (namespace `launcher`), asi que el cronometro sigue contando mientras hay una app abierta.

Codigo: `components/launcher/os_ui.c` (pila de pantallas, foco, tarea del sistema: botones, apagado, sueno), una pantalla por fichero (`os_face.c`, `os_menu.c`, `os_apps.c`, `os_settings.c`, `os_tools.c`, `os_timer.c`, `os_alarms.c`, `os_usb.c`) y `os_alerts.c` (modelo, NVS y sonido del temporizador y las alarmas) y `os_store.c` (NVS).

## Como funciona

Cada app sigue siendo su propio firmware, en su propia particion de app (slot OTA). El launcher es la app `factory`.

1. El launcher lista los slots que contienen una app valida: nombre (`project_name` sin el prefijo `ESP32Watch`) y version.
2. Al elegir una, el launcher apunta el siguiente arranque a su slot (`esp_ota_set_boot_partition`, que ademas verifica la imagen) y reinicia.
3. Lo primero que hace la app es `watch_launcher_boot_once()`, que devuelve el siguiente arranque al launcher.

Asi, cualquier reinicio vuelve al launcher: la opcion "Salir" de la app, un cuelgue, "Quit Game" en Doom o un apagado con PWR (6 s). Nunca te quedas atrapado en una app que falla.

Cambiar de app es un reinicio (~1-2 s), asi que cada app arranca limpia: RAM, PSRAM, tareas, DMA, LVGL y display. Lo que deba persistir va en NVS (compartida, un namespace por app) o en la SD.

La seccion Apps recuerda la ultima app abierta.

Salir desde cada app:

| App | Como volver |
| --- | --- |
| Maze | Boton "Salir" en el menu principal |
| Fluid | "Salir al launcher" en el menu de `PWR` (guarda antes los ajustes) |
| Doom | Menu del juego > "Quit Game" |
| Recorder | `PWR` en la pantalla Grabar (sin grabar) |

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
| `ota_3` | `0x7a0000` | 2 MB | Recorder |
| `ota_4` a `ota_6` | `0x9a0000` a `0xda0000` | 2 MB c/u | Libres (apps futuras) |
| `storage` | `0x1000000` | 16 MB | WAD de Doom embebido (FAT, solo lectura); sin uso si Doom lo lee de la SD |

La flash es de 32 MB y esta configurada asi. Todo el codigo (launcher y apps) queda por debajo de los 16 MB porque ejecutar codigo por encima es una funcion experimental de ESP-IDF; por encima solo va `storage`, que se lee por la API de particiones (validado en placa: escritura, lectura y montaje de un FAT). Cada app ocupa hoy entre 0,7 y 0,9 MB de sus 2 MB, y el launcher ~0,9 MB de 1,5 MB. Cambiar la tabla implica actualizar sus copias en cada app y en `apps.conf`.

## Grabar todo

```sh
source "$HOME/.espressif/tools/activate_idf_v5.5.4.sh"
cp .env.example .env    # y ajustar si hace falta
idf.py set-target esp32s3   # solo la primera vez
./flash_all.sh
```

`flash_all.sh` compila el launcher y cada app de `apps.conf`, y graba bootloader, tabla, `otadata` en blanco, launcher, las apps y, si Doom lo embebe, la imagen FAT de su WAD. Todo en una sola pasada de esptool. Los logs de compilacion quedan en `build/flash_all_<app>.log`.

| Uso | Que hace |
| --- | --- |
| `./flash_all.sh` | Compila y graba todo |
| `./flash_all.sh fluid` | Compila y regraba solo esa app (un nombre de `apps.conf` o `launcher`), sin tocar las demas; la graba aunque no este en `APPS` |
| `./flash_all.sh --no-build` | Graba lo ya compilado en cada `build/` |

Si falta el repo de una app, o no compila, `flash_all.sh` (modo completo) deja su slot vacio y el launcher no lo muestra. La regrabacion de una sola app supone que el reloj ya tiene esta tabla (un `./flash_all.sh` completo previo). Si el `partitions.csv` de una app no coincide con el de aqui, el script avisa (la grabacion sigue: el reloj usa la tabla del launcher, pero la app en standalone no).

### `.env`

| Variable | Defecto | Uso |
| --- | --- | --- |
| `WATCH_PORT` | vacio (autodetectar) | Puerto serie, p. ej. `/dev/tty.usbmodem1101` |
| `APPS` | vacio (todas) | Apps de `apps.conf` que se graban, separadas por espacios, p. ej. `APPS="maze fluid"`. Las que no estan se vacian de su slot en un `./flash_all.sh` completo (el launcher deja de mostrarlas) y, si falta Doom, no se graba su WAD |
| `<APP>_DIR` (`MAZE_DIR`, `DOOM_DIR`...) | el de `apps.conf` | Repo de cada app, relativo a este o absoluto |

### WAD de Doom

Lo decide Doom, no el launcher: si su build lo embebe (WAD en `ESP32Watch-Doom/wad/` y `CONFIG_DOOM_EMBED_WAD=y`, ver su `wad/README.md`), deja `build/storage.bin` y `flash_all.sh` lo graba en `storage`. Si no, el script avisa y Doom lee el WAD de la microSD. Un WAD embebido antiguo que quede en `storage` se ignora: Doom solo monta `/internal` cuando se compilo con WAD embebido.

## Anadir una app nueva

El checklist de la app (arranque, "Salir", NVS, tabla) esta en el README de [ESP32Watch-template](https://github.com/Sethyrus/ESP32Watch-template#crear-una-app-nueva-desde-esta-plantilla). Aqui solo:

1. Un slot libre de `partitions.csv` (`ota_4` a `ota_6` estan vacios). Si se acaban, hay que redisenar la tabla sin pasar el codigo de los 16 MB, y copiarla a cada app.
2. Una linea en `apps.conf`: nombre, slot y repo.

El launcher la muestra sola: lista todo slot con una imagen valida. Su color, icono y descripcion salen de `KNOWN` en `components/launcher/os_apps.c` (si no esta, un color neutro, su inicial y su version).

## Documentacion

Hardware, entorno y convenciones: [ESP32Watch-core/docs](https://github.com/Sethyrus/ESP32Watch-core/tree/main/docs).

## Licencia

MIT. Ver [LICENSE](LICENSE).
