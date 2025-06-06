#!/bin/bash
# Set up PlatformIO and build the firmware
set -e

# Detect platformio
if ! command -v pio >/dev/null 2>&1; then
    echo "Installing PlatformIO CLI..."
    python3 -m pip install --user -U platformio
    export PATH="$PATH:$(python3 -m site --user-base)/bin"
fi

# Build firmware
pio run
