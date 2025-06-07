#!/usr/bin/env bash
# Set up PlatformIO and build the firmware
set -Eeuo pipefail

error_exit() {
    echo "Error on line $1: $BASH_COMMAND" >&2
    exit 1
}
trap 'error_exit $LINENO' ERR

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
        ssid_escaped=$(escape_sed_replacement "$wifi_ssid")
        pass_escaped=$(escape_sed_replacement "$wifi_pass")
        sed_inplace "s|#define WIFI_SSID .*|#define WIFI_SSID \"${ssid_escaped}\"|" include/secrets.h
        sed_inplace "s|#define WIFI_PASSWORD .*|#define WIFI_PASSWORD \"${pass_escaped}\"|" include/secrets.h
        echo "Created include/secrets.h"
    else
        echo "Please create include/secrets.h before proceeding." >&2
        exit 1
    fi
fi

# Build firmware
if ! pio lib -g install fastled/FastLED@3.9.20; then
    echo "Library installation failed" >&2
    exit 1
fi
if ! pio run; then
    echo "Build failed" >&2
    exit 1
fi
