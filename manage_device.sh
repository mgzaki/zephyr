#!/bin/bash
# manage_device.sh
# Universally reusable script to build, flash, and debug Zephyr applications
# for the Seeed Studio XIAO nRF52840 (and others) using a CMSIS-DAP debugger (e.g. XIAO Debug Mate).

set -e

# --- Configuration ---
# Board configuration (can be overridden via environment variable)
BOARD="${BOARD:-xiao_ble/nrf52840/sense}"

# Toolchain environment configuration
TOOLCHAIN_HASH="2ac5840438"
TOOLCHAIN_DIR="/home/msamory/ncs/toolchains/${TOOLCHAIN_HASH}"
export PATH="${TOOLCHAIN_DIR}/usr/local/bin:${TOOLCHAIN_DIR}/usr/bin:${PATH}"
export PYTHONPATH="${TOOLCHAIN_DIR}/usr/local/lib/python3.12/site-packages"
export ZEPHYR_BASE="/home/msamory/ncs/v3.2.3/zephyr"

# OpenOCD configuration for CMSIS-DAP
# The XIAO Debug Mate uses CMSIS-DAP over SWD. We reduce the adapter speed for stability.
OPENOCD_ARGS="--cmd-pre-init \"source [find interface/cmsis-dap.cfg]\" "
OPENOCD_ARGS+="--cmd-pre-init \"transport select swd\" "
OPENOCD_ARGS+="--cmd-pre-init \"adapter speed 1000\" "
OPENOCD_ARGS+="--cmd-pre-init \"source [find target/nrf52.cfg]\""
# ---------------------

function print_usage() {
    echo "Usage: $0 [command] [app_dir]"
    echo ""
    echo "Commands:"
    echo "  build   - Build the application for ${BOARD}"
    echo "  flash   - Flash the compiled binary using OpenOCD and CMSIS-DAP"
    echo "  run     - Build the application and then flash it"
    echo "  debug   - Start an interactive debug session using OpenOCD and GDB"
    echo "  clean   - Remove the 'build/' and 'build_1/' directories to start fresh"
    echo ""
    echo "Arguments:"
    echo "  [app_dir] - Optional. Path to the Zephyr application folder (defaults to current directory '.')"
    echo ""
    echo "Environment check:"
    echo "  Ensure that your XIAO Debug Mate is connected via USB and recognized."
    echo "  You may need '60-openocd.rules' installed in /etc/udev/rules.d/ for permissions."
    echo ""
    echo "Overrides: You can override the target board by prepending BOARD=..., e.g.:"
    echo "  BOARD=nrf52840dk_nrf52840 ./manage_device.sh run"
}

if [ $# -eq 0 ]; then
    print_usage
    exit 1
fi

COMMAND=$1
APP_DIR="${2:-.}"

if [ ! -d "$APP_DIR" ]; then
    echo "Error: Application directory '$APP_DIR' does not exist."
    exit 1
fi

# Move into the app directory so west executes in the right place
cd "$APP_DIR"

case "$COMMAND" in
    build)
        echo "=> Building the application in '$PWD' for ${BOARD}..."
        west build -b ${BOARD} --pristine
        ;;
    flash)
        echo "=> Flashing the device with OpenOCD..."
        # Use eval to properly expand the quoted OPENOCD_ARGS
        eval "west flash -r openocd ${OPENOCD_ARGS}"
        ;;
    run)
        echo "=> Building and flashing the application in '$PWD' for ${BOARD}..."
        west build -b ${BOARD} --pristine
        eval "west flash -r openocd ${OPENOCD_ARGS}"
        ;;
    debug)
        echo "=> Starting debug session with OpenOCD..."
        eval "west debug -r openocd ${OPENOCD_ARGS}"
        ;;
    clean)
        echo "=> Cleaning build directories in '$PWD'..."
        rm -rf build/ build_1/
        echo "Done."
        ;;
    *)
        echo "Unknown command: ${COMMAND}"
        print_usage
        exit 1
        ;;
esac
