#!/bin/bash
# This script MUST be run in the same directory as your project code
# (also known in Mbed Studio as a workspace)
echo

if [ -z "$1" ] || [ -z "$2" ]; then
    echo "Missing arguments. Command syntax:"
    echo
    echo "./arduino-upload.sh [Serial Port] [Binary_file_to_upload].bin"
    exit 1
fi

serial_port=""
bin_to_load=""

# Validate the first argument looks like a Linux serial device
# (e.g. /dev/ttyACM0, /dev/ttyUSB0)
if [[ "$1" =~ ^/dev/tty(ACM|USB)[0-9]+$ ]]; then
    serial_port="$1"
fi

if [ -z "$serial_port" ]; then
    echo "The first argument must be a valid serial port (e.g. /dev/ttyACM0)."
    echo "Exiting..."
    exit 2
fi

# Validate the second argument contains "bin"
if [[ "$2" == *bin* ]]; then
    bin_to_load="$2"
fi

if [ -z "$bin_to_load" ]; then
    echo "The second argument must be a binary (.bin) file."
    echo "Exiting..."
    exit 3
fi

# Search recursively for the actual build output matching the binary name,
# inside a BUILD/ARDUINO_NANO33BLESENSE directory
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
found_file=$(find "$script_dir" -type f -name "*$2" -path "*BUILD*" -path "*ARDUINO_NANO33BLESENSE*" | head -n 1)

if [ -n "$found_file" ]; then
    bin_to_load="$found_file"
fi

echo "Found binary file: \"$bin_to_load\""
echo "Uploading..."
arduino-cli upload -p "$serial_port" -b arduino:mbed_nano:nano33ble -i "$bin_to_load" -t -v
