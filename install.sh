#!/usr/bin/env bash
# Install and flash script for ESP32
set -e

# Ensure PlatformIO is installed
if ! command -v pio >/dev/null 2>&1; then
    echo "Installing PlatformIO CLI..."
    if ! python3 -m pip install --user -U platformio; then
        echo "Failed to install PlatformIO." >&2
        echo "Please check your network connection or install it manually:" >&2
        echo "https://docs.platformio.org/en/latest/core/installation.html" >&2
        exit 1
    fi
    export PATH="$PATH:$(python3 -m site --user-base)/bin"
fi

# Verify secrets
if [ ! -f include/secrets.h ]; then
    echo "include/secrets.h not found." >&2
    echo "Copy include/secrets_example.h to include/secrets.h and set your WiFi credentials." >&2
    exit 1
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
