#!/usr/bin/env bash
# Builds the launcher and the apps listed in apps.conf and flashes them into the
# shared layout (partitions.csv). Settings come from .env (see .env.example).
#
#   ./flash_all.sh                 build everything, flash everything
#   ./flash_all.sh fluid           build and flash only that app (a name from apps.conf, or launcher)
#   ./flash_all.sh --no-build ...  flash the existing build/ outputs
#
# Needs the ESP-IDF 5.5.4 environment (source "$HOME/.espressif/v5.5.4/esp-idf/export.sh").
set -euo pipefail

ROOT="$(cd "$(dirname "$0")" && pwd)"
APPS_CONF="$ROOT/apps.conf"

# Field N (1 name, 2 slot, 3 default dir) of an app in apps.conf.
conf_field() {
    awk -v name="$1" -v col="$2" '
        /^[[:space:]]*(#|$)/ { next }
        $1 == name { print $col; exit }
    ' "$APPS_CONF"
}
app_names() {
    awk '/^[[:space:]]*(#|$)/ { next } { print $1 }' "$APPS_CONF"
}

BUILD=1
TARGET=all
for arg in "$@"; do
    case "$arg" in
        --no-build) BUILD=0 ;;
        all | launcher) TARGET="$arg" ;;
        -h | --help) sed -n '2,9p' "$0"; exit 0 ;;
        *)
            if [ -n "$(conf_field "$arg" 1)" ]; then
                TARGET="$arg"
            else
                echo "Unknown argument: $arg (see --help and apps.conf)" >&2
                exit 1
            fi
            ;;
    esac
done

if [ -f "$ROOT/.env" ]; then
    set -a
    # shellcheck disable=SC1091
    . "$ROOT/.env"
    set +a
fi
WATCH_PORT="${WATCH_PORT:-}"
TABLE="$ROOT/partitions.csv"

# idf.py is a shell function in some ESP-IDF activation scripts, so it is not
# inherited by this script: call the tool through IDF_PATH instead.
if [ -z "${IDF_PATH:-}" ] || [ ! -f "$IDF_PATH/tools/idf.py" ]; then
    echo "ESP-IDF not active: source \"\$HOME/.espressif/v5.5.4/esp-idf/export.sh\" first" >&2
    exit 1
fi
idf() { python "$IDF_PATH/tools/idf.py" "$@"; }
if command -v esptool.py >/dev/null 2>&1; then
    ESPTOOL=(esptool.py)
else
    ESPTOOL=(python -m esptool)
fi

# app name -> slot label and repo dir (<NAME>_DIR from .env wins over apps.conf).
slot_of() { conf_field "$1" 2; }
dir_var() { echo "$(echo "$1" | tr a-z A-Z)_DIR"; }
dir_of() {
    local dir var
    if [ "$1" = launcher ]; then
        dir="$ROOT"
    else
        var="$(dir_var "$1")"
        dir="${!var:-$(conf_field "$1" 3)}"
    fi
    case "$dir" in
        /*) echo "$dir" ;;
        *) echo "$ROOT/$dir" ;;
    esac
}

# Partition table without comments or blanks, to compare copies.
table_rows() {
    awk '/^[[:space:]]*(#|$)/ { next } { gsub(/[[:space:]]/, ""); print }' "$1"
}

# Column 4 (offset) or 5 (size) of a partition in the shared table.
part_field() {
    awk -F, -v name="$1" -v col="$2" '
        /^[[:space:]]*#/ { next }
        { gsub(/[[:space:]]/, "") }
        $1 == name { print $col; exit }
    ' "$TABLE"
}

build() {
    local name="$1" dir
    dir="$(dir_of "$name")"
    [ "$BUILD" = 1 ] || return 0
    echo "== Building $name ($dir)"
    (cd "$dir" && idf build) >"$ROOT/build/flash_all_$name.log" 2>&1 || {
        echo "Build of $name failed, see build/flash_all_$name.log" >&2
        return 1
    }
}

app_bin() {
    local dir
    dir="$(dir_of "$1")"
    [ -f "$dir/build/flash_app_args" ] || return 1
    echo "$dir/build/$(tail -n 1 "$dir/build/flash_app_args" | awk '{print $2}')"
}

FLASH_ARGS=()
add() { FLASH_ARGS+=("$1" "$2"); }

add_app() {
    local name="$1" slot offset size bin bytes
    slot="$(slot_of "$name")"
    offset="$(part_field "$slot" 4)"
    size="$(part_field "$slot" 5)"
    if ! bin="$(app_bin "$name")" || [ ! -f "$bin" ]; then
        return 1
    fi
    bytes=$(wc -c <"$bin" | tr -d ' ')
    if [ "$bytes" -gt $((size)) ]; then
        echo "$name: $bytes bytes does not fit slot $slot ($((size)) bytes)" >&2
        return 1
    fi
    echo "   $name -> $slot @ $offset ($bytes bytes)"
    if [ "$(table_rows "$(dir_of "$name")/partitions.csv" 2>/dev/null)" != "$(table_rows "$TABLE")" ]; then
        echo "   WARNING: $name/partitions.csv differs from the launcher's; copy it from here" >&2
    fi
    add "$offset" "$bin"
}

# Blank first sector: the launcher then shows the slot as empty.
add_blank_slot() {
    local name="$1" slot offset
    slot="$(slot_of "$name")"
    offset="$(part_field "$slot" 4)"
    echo "   $name missing: slot $slot left empty"
    add "$offset" "$BLANK"
}

mkdir -p "$ROOT/build"
BLANK="$ROOT/build/blank_sector.bin"
head -c 4096 /dev/zero | LC_ALL=C tr '\0' '\377' >"$BLANK"

if [ "$TARGET" = all ] || [ "$TARGET" = launcher ]; then
    build launcher
    L="$ROOT/build"
    [ -f "$L/ota_data_initial.bin" ] || { echo "Launcher not built (run without --no-build)" >&2; exit 1; }
    python "$IDF_PATH/components/partition_table/gen_esp32part.py" --flash-size 16MB -q \
        "$TABLE" "$L/flash_all_partitions.bin"
    add 0x0 "$L/bootloader/bootloader.bin"
    add 0x8000 "$L/flash_all_partitions.bin"
    add "$(part_field otadata 4)" "$L/ota_data_initial.bin"
    add "$(part_field factory 4)" "$(app_bin launcher)"
fi

for name in $(app_names); do
    [ "$TARGET" = all ] || [ "$TARGET" = "$name" ] || continue
    dir="$(dir_of "$name")"
    if [ ! -d "$dir" ]; then
        [ "$TARGET" = all ] && add_blank_slot "$name" && continue
        echo "$name: $dir not found (set $(dir_var "$name") in .env)" >&2
        exit 1
    fi
    if ! build "$name" || ! add_app "$name"; then
        [ "$TARGET" = all ] && add_blank_slot "$name" && continue
        exit 1
    fi
done

# Doom decides whether its WAD is embedded (wad/ + CONFIG_DOOM_EMBED_WAD); its build
# only leaves build/storage.bin when it is.
if [ "$TARGET" = all ] || [ "$TARGET" = doom ]; then
    wad_image="$(dir_of doom)/build/storage.bin"
    if [ -f "$wad_image" ]; then
        echo "   Doom WAD image -> storage @ $(part_field storage 4)"
        add "$(part_field storage 4)" "$wad_image"
    else
        echo "   Doom without an embedded WAD: it will load it from the microSD"
    fi
fi

PORT_ARGS=()
[ -n "$WATCH_PORT" ] && PORT_ARGS=(-p "$WATCH_PORT")
echo "== Flashing"
"${ESPTOOL[@]}" --chip esp32s3 "${PORT_ARGS[@]+"${PORT_ARGS[@]}"}" -b 921600 \
    --before default_reset --after hard_reset \
    write_flash --flash_mode dio --flash_freq 80m --flash_size 16MB "${FLASH_ARGS[@]}"
echo "== Done"
