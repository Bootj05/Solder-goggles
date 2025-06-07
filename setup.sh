#!/bin/bash
# Set up PlatformIO and build the firmware
set -e

# Determine sed command and in-place arguments
SED="sed"
if command -v gsed >/dev/null 2>&1; then
    SED="gsed"
fi
if "$SED" --version 2>/dev/null | grep -q "GNU"; then
    SED_INPLACE_ARGS=(-i)
else
    SED_INPLACE_ARGS=(-i '')
fi
sed_inplace() {
    "$SED" "${SED_INPLACE_ARGS[@]}" "$@"
}

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

# Detect platformio
if ! command -v pio >/dev/null 2>&1; then
    echo "Installing PlatformIO CLI..."
    if ! command -v python3 >/dev/null 2>&1; then
        echo "python3 is required but not found. Please install Python 3 and pip." >&2
        exit 1
    fi
    if ! python3 -m pip install --user -U platformio; then
        echo "Failed to install PlatformIO." >&2
        echo "Please check your network connection or install it manually:" >&2
        echo "https://docs.platformio.org/en/latest/core/installation.html" >&2
        exit 1
    fi
    if [ -z "$VIRTUAL_ENV" ]; then
        export PATH="$PATH:$(python3 -m site --user-base)/bin"
    fi
fi

# Verify secrets.h exists before building
if [ ! -f include/secrets.h ]; then
    echo "include/secrets.h not found."
    read -p "Create it now? [y/N] " create_secrets
    if [[ $create_secrets =~ ^[Yy]$ ]]; then
        cp include/secrets_example.h include/secrets.h
        read -p "WiFi SSID: " wifi_ssid
        read -p "WiFi password: " wifi_pass
        sed_inplace "s|#define WIFI_SSID .*|#define WIFI_SSID \"${wifi_ssid}\"|" include/secrets.h
        sed_inplace "s|#define WIFI_PASSWORD .*|#define WIFI_PASSWORD \"${wifi_pass}\"|" include/secrets.h
        echo "Created include/secrets.h"
    else
        echo "Please create include/secrets.h before proceeding." >&2
        exit 1
    fi
fi

# Build firmware
pio run
