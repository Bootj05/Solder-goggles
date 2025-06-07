#!/usr/bin/env bash
# Install and flash script for ESP32
set -e

# Offer a Python virtual environment
read -p "Use a Python virtual environment? [y/N] " use_venv
if [[ $use_venv =~ ^[Yy]$ ]]; then
    read -p "Path to virtual environment (default .venv): " venv_path
    venv_path=${venv_path:-.venv}
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
    pip_opts=""
    if [ -z "$VIRTUAL_ENV" ]; then
        pip_opts="--user"
    fi
    if ! python3 -m pip install $pip_opts -U platformio; then
        echo "Failed to install PlatformIO." >&2
        echo "Please check your network connection or install it manually:" >&2
        echo "https://docs.platformio.org/en/latest/core/installation.html" >&2
        exit 1
    fi
    if [ -z "$VIRTUAL_ENV" ]; then
        export PATH="$PATH:$(python3 -m site --user-base)/bin"
    fi
fi

# Verify secrets
if [ ! -f include/secrets.h ]; then
    echo "include/secrets.h not found."
    read -p "Create it now? [y/N] " create_secrets
    if [[ $create_secrets =~ ^[Yy]$ ]]; then
        cp include/secrets_example.h include/secrets.h
        read -p "WiFi SSID: " wifi_ssid
        read -p "WiFi password: " wifi_pass
        sed -i "s|#define WIFI_SSID .*|#define WIFI_SSID \"${wifi_ssid}\"|" include/secrets.h
        sed -i "s|#define WIFI_PASSWORD .*|#define WIFI_PASSWORD \"${wifi_pass}\"|" include/secrets.h
        echo "Created include/secrets.h"
    else
        echo "Please create include/secrets.h before proceeding." >&2
        exit 1
    fi
fi

# Build firmware for esp32 environment
pio run -e esp32

BIN_PATH=.pio/build/esp32/firmware.bin
if [ -f "$BIN_PATH" ]; then
    read -p "Export firmware binary to firmware.bin? [y/N] " export_bin
    if [[ $export_bin =~ ^[Yy]$ ]]; then
        cp "$BIN_PATH" firmware.bin
        echo "Firmware binary exported to firmware.bin"
    fi
fi

# Search for ESP32 serial ports
echo "Searching for connected ESP32 boards..."
mapfile -t ports < <(pio device list | awk '/tty/ {print $1}')

if [ ${#ports[@]} -eq 0 ]; then
    echo "No serial ports detected." >&2
    exit 1
fi

selected="${ports[0]}"
if [ ${#ports[@]} -gt 1 ]; then
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

read -p "Flash firmware to $selected? [y/N] " confirm
if [[ $confirm =~ ^[Yy]$ ]]; then
    pio run -e esp32 --target upload --upload-port "$selected"
else
    echo "Flash cancelled."
fi
