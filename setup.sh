#!/bin/bash
# Set up PlatformIO and build the firmware
set -e

# Detect platformio
if ! command -v pio >/dev/null 2>&1; then
    echo "Installing PlatformIO CLI..."
    python3 -m pip install --user -U platformio
    export PATH="$PATH:$(python3 -m site --user-base)/bin"
fi

# Verify secrets.h exists before building
if [ ! -f include/secrets.h ]; then
    echo "include/secrets.h not found." >&2
    echo "Copy include/secrets_example.h to include/secrets.h and set your WiFi credentials." >&2
    exit 1
fi

# Build firmware
pio run
