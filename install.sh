#!/usr/bin/env bash
# Install and flash script for ESP32
set -Eeuo pipefail

NON_INTERACTIVE=false

error_exit() {
    echo "Error on line $1: $BASH_COMMAND" >&2
    exit 1
}
trap 'error_exit $LINENO' ERR

# Always pause before exiting so the terminal stays open on failure or success

# Determine sed command and in-place arguments
SED="sed"
if command -v gsed >/dev/null 2>&1; then
    SED="gsed"
fi

HAS_GNU_SED=false
if "$SED" --version 2>/dev/null | grep -q "GNU"; then
    HAS_GNU_SED=true
fi

sed_inplace() {
    local expr="$1"
    local file="$2"
    if $HAS_GNU_SED; then
        "$SED" -i -e "$expr" "$file"
    elif [[ "$(uname)" == "Darwin" ]]; then
        "$SED" -i '' -e "$expr" "$file"
    else
        local tmp
        tmp=$(mktemp)
        "$SED" -e "$expr" "$file" > "$tmp"
        mv "$tmp" "$file"
    fi
}

# Escape special characters for sed replacement
escape_sed_replacement() {
    printf '%s' "$1" | sed -e 's/[\\/&|]/\\&/g'
}

# Display help text
show_help() {
    cat <<EOF
Usage: $0 [OPTION]

Steps performed by the script:
 1. Offer to create a Python virtual environment
 2. Install PlatformIO if missing
 3. Build the firmware for the esp32 environment
 4. Optionally export the firmware binary
 5. Detect a connected ESP32 and flash it

Each step prompts interactively. Answer 'n' or press Enter to skip an optional action.
 -y, --non-interactive  Assume default answers and skip pauses
EOF
}

# Parse command-line arguments
for arg in "$@"; do
    case $arg in
        -h|--help)
            show_help
            exit 0
            ;;
        -y|--non-interactive|--yes)
            NON_INTERACTIVE=true
            shift
            ;;
        *)
            echo "Unknown option: $arg" >&2
            show_help
            exit 1
            ;;
    esac
done

if [ "$NON_INTERACTIVE" = false ]; then
    trap 'echo; read -n 1 -r -p "Press any key to exit..."' EXIT
fi


# Offer a Python virtual environment
if [ "$NON_INTERACTIVE" = true ]; then
    use_venv="n"
else
    read -p "Use a Python virtual environment? [y/N] " use_venv
fi
if [[ $use_venv =~ ^[Yy]$ ]]; then
    if [ "$NON_INTERACTIVE" = true ]; then
        venv_path=.venv
    else
        read -p "Path to virtual environment (default .venv): " venv_path
        venv_path=${venv_path:-.venv}
    fi
    if [ ! -d "$venv_path" ]; then
        echo "Creating virtual environment at $venv_path..."
        python3 -m venv "$venv_path"
    fi
    # shellcheck disable=SC1090
    source "$venv_path/bin/activate"
fi

# Ensure PlatformIO is installed
if ! command -v pio >/dev/null 2>&1; then
    echo "Installing PlatformIO CLI..."
    if ! command -v python3 >/dev/null 2>&1; then
        echo "python3 is required to install PlatformIO." >&2
        exit 1
    fi
    pip_opts=""
    if [ -z "${VIRTUAL_ENV:-}" ]; then
        pip_opts="--user"
    fi
    if ! python3 -m pip install $pip_opts -U platformio; then
        echo "Failed to install PlatformIO." >&2
        echo "Please check your network connection or install it manually:" >&2
        echo "https://docs.platformio.org/en/latest/core/installation.html" >&2
        exit 1
    fi
    if [ -z "${VIRTUAL_ENV:-}" ]; then
        export PATH="$PATH:$(python3 -m site --user-base)/bin"
    fi
fi

# Verify secrets
if [ ! -f include/secrets.h ]; then
    echo "include/secrets.h not found."
    if [ "$NON_INTERACTIVE" = true ]; then
        create_secrets="n"
    else
        read -p "Create it now? [y/N] " create_secrets
    fi
    if [[ $create_secrets =~ ^[Yy]$ ]]; then
        cp include/secrets_example.h include/secrets.h
        if [ "$NON_INTERACTIVE" = true ]; then
            echo "Created include/secrets.h with example values"
        else
            read -p "WiFi SSID: " wifi_ssid
            read -s -p "WiFi password: " wifi_pass; echo
            ssid_escaped=$(escape_sed_replacement "$wifi_ssid")
            pass_escaped=$(escape_sed_replacement "$wifi_pass")
            sed_inplace "s|#define WIFI_SSID .*|#define WIFI_SSID \"${ssid_escaped}\"|" include/secrets.h
            sed_inplace "s|#define WIFI_PASSWORD .*|#define WIFI_PASSWORD \"${pass_escaped}\"|" include/secrets.h
            echo "Created include/secrets.h"
        fi
    else
        echo "Please create include/secrets.h before proceeding." >&2
        exit 1
    fi
fi

# Allow user to override hardware pins and hostname
if [ "$NON_INTERACTIVE" = true ]; then
    led_pin=2
    btn_prev=0
    btn_next=35
    mdns_name=JohannesBril
    btn_hold=17
else
    read -p "LED pin (default 2): " led_pin
    led_pin=${led_pin:-2}
    read -p "Previous button pin (default 0): " btn_prev
    btn_prev=${btn_prev:-0}
    read -p "Next button pin (default 35): " btn_next
    btn_next=${btn_next:-35}
    read -p "mDNS hostname (default JohannesBril): " mdns_name
    mdns_name=${mdns_name:-JohannesBril}
    read -p "Hold button pin (default 17): " btn_hold
    btn_hold=${btn_hold:-17}
fi
mdns_escaped=$(escape_sed_replacement "$mdns_name")
sed_inplace "s|constexpr uint8_t LED_PIN = .*;|constexpr uint8_t LED_PIN = ${led_pin};|" src/goggles.ino
sed_inplace "s|constexpr uint8_t BTN_PREV = .*;|constexpr uint8_t BTN_PREV = ${btn_prev};|" src/goggles.ino
sed_inplace "s|constexpr uint8_t BTN_NEXT = .*;|constexpr uint8_t BTN_NEXT = ${btn_next};|" src/goggles.ino
sed_inplace "s|constexpr uint8_t BTN_HOLD = .*;|constexpr uint8_t BTN_HOLD = ${btn_hold};|" src/goggles.ino
sed_inplace "s|constexpr char DEFAULT_HOST\[] = \".*\";|constexpr char DEFAULT_HOST[] = \"${mdns_escaped}\";|" src/goggles.ino

# Build firmware for esp32 environment
if ! pio pkg install --global \
        -l fastled/FastLED@3.9.20 \
        -l links2004/WebSockets; then
    echo "Library installation failed" >&2
    exit 1
fi
if ! pio run -e esp32; then
    echo "Build failed" >&2
    exit 1
fi

BIN_PATH=.pio/build/esp32/firmware.bin
if [ -f "$BIN_PATH" ]; then
    if [ "$NON_INTERACTIVE" = true ]; then
        export_bin="n"
    else
        read -p "Export firmware binary to firmware.bin? [y/N] " export_bin
    fi
    if [[ $export_bin =~ ^[Yy]$ ]]; then
        cp "$BIN_PATH" firmware.bin
        echo "Firmware binary exported to firmware.bin"
    fi
fi

# Search for ESP32 serial ports
echo "Searching for connected ESP32 boards..."
mapfile -t ports < <(pio device list | awk '/tty|COM/ {print $1}')

if [ ${#ports[@]} -eq 0 ]; then
    echo "No serial ports detected. Connect your ESP32 and ensure drivers are installed." >&2
    exit 1
fi

selected="${ports[0]}"
if [ ${#ports[@]} -gt 1 ]; then
    if [ "$NON_INTERACTIVE" = true ]; then
        echo "Multiple ports detected, using ${ports[0]}"
    else
        echo "Multiple ports detected:"
        select port in "${ports[@]}" "Quit"; do
            if [[ $REPLY -ge 1 && $REPLY -le ${#ports[@]} ]]; then
                selected="${ports[$REPLY-1]}"
                break
            else
                echo "Aborting."; exit 1
            fi
        done
    fi
fi

if [ "$NON_INTERACTIVE" = true ]; then
    confirm="n"
else
    read -p "Flash firmware to $selected? [y/N] " confirm
fi
if [[ $confirm =~ ^[Yy]$ ]]; then
    echo "Writing firmware..."
    if ! pio run -e esp32 --target upload --upload-port "$selected"; then
        echo "Flashing failed." >&2
        exit 1
    fi
    echo "Flash complete."
else
    echo "Flash cancelled."
fi
