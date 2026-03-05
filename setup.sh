#!/bin/bash
# Setup script for Arduboy development with arduino-cli
# Run this once to install everything needed

set -e

echo "=== Arduboy Development Setup ==="
echo ""

# Check if arduino-cli is installed
if ! command -v arduino-cli &> /dev/null; then
    echo "Installing arduino-cli..."
    # Download latest arduino-cli for Windows
    curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
    export PATH="$PATH:$HOME/bin"
    echo ""
    echo "arduino-cli installed to ~/bin/arduino-cli"
    echo "Add to your PATH: export PATH=\"\$PATH:\$HOME/bin\""
else
    echo "arduino-cli already installed: $(which arduino-cli)"
fi

echo ""
echo "--- Installing Arduino AVR core (for Arduboy/Leonardo) ---"
arduino-cli core update-index
arduino-cli core install arduino:avr

echo ""
echo "--- Installing Arduboy2 library ---"
arduino-cli lib install Arduboy2

echo ""
echo "--- Verifying installation ---"
echo "Installed cores:"
arduino-cli core list
echo ""
echo "Installed libraries:"
arduino-cli lib list | grep -i arduboy

echo ""
echo "--- Checking connected boards ---"
arduino-cli board list

echo ""
echo "=== Setup complete! ==="
echo ""
echo "To compile:  arduino-cli compile --fqbn arduino:avr:leonardo SpaceDodge/"
echo "To upload:   arduino-cli compile --fqbn arduino:avr:leonardo -u -p COM3 SpaceDodge/"
