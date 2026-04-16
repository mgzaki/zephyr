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
    echo "  build       - Build the application for ${BOARD}"
    echo "  flash       - Flash the compiled binary using OpenOCD and CMSIS-DAP"
    echo "  run         - Build the application and then flash it"
    echo "  debug       - Start an interactive debug session using OpenOCD and GDB"
    echo "  setup-debug - Configure VS Code for visual debugging (Cortex-Debug)"
    echo "  clean       - Remove the 'build/' and 'build_1/' directories to start fresh"
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

# ---------------------------------------------------------------------------
# setup-debug: Auto-configure VS Code for visual debugging with Cortex-Debug.
#
# What it does:
#   1. Finds a working arm GDB binary (Zephyr SDK, system, or snap — skipping
#      snap builds that lack network access).
#   2. Locates the built ELF file in the build directory.
#   3. Installs udev rules if not already present (requires sudo).
#   4. Kills any stale OpenOCD processes that would block the GDB port.
#   5. Generates .vscode/launch.json with the correct paths.
#
# Portability: uses only the BOARD variable and auto-detected paths so it
# works across projects and machines without hard-coded values.
# ---------------------------------------------------------------------------
function setup_debug() {
    echo "=> Configuring VS Code debug environment..."

    # ── 1. Find a usable GDB ────────────────────────────────────────────
    local gdb_path=""
    local candidates=()

    # Prefer the Zephyr SDK installed under the user's home directory
    while IFS= read -r -d '' p; do
        candidates+=("$p")
    done < <(find "${HOME}" -maxdepth 6 -path "*/arm-zephyr-eabi/bin/arm-zephyr-eabi-gdb" -type f -print0 2>/dev/null)

    # Also consider system-wide gdb-multiarch
    if command -v gdb-multiarch &>/dev/null; then
        candidates+=("$(command -v gdb-multiarch)")
    fi

    # Also consider arm-none-eabi-gdb on PATH
    if command -v arm-none-eabi-gdb &>/dev/null; then
        candidates+=("$(command -v arm-none-eabi-gdb)")
    fi

    for c in "${candidates[@]}"; do
        # Skip snap-confined binaries — they can't open TCP sockets to OpenOCD
        case "$c" in /snap/*) continue ;; esac

        if "$c" --version &>/dev/null; then
            gdb_path="$c"
            break
        fi
    done

    if [ -z "$gdb_path" ]; then
        echo "ERROR: No usable ARM GDB found."
        echo "Install the Zephyr SDK (https://docs.zephyrproject.org/latest/develop/toolchains/zephyr_sdk.html)"
        echo "or run:  sudo apt install gdb-multiarch"
        exit 1
    fi
    echo "   GDB: ${gdb_path}"

    # ── 2. Find the ELF file ────────────────────────────────────────────
    local elf_path=""
    # The Zephyr build system places the ELF under build/<app>/zephyr/zephyr.elf
    # or under build/zephyr/zephyr.elf depending on sysbuild usage.
    for candidate_elf in \
        build/*/zephyr/zephyr.elf \
        build/zephyr/zephyr.elf; do
        if [ -f "$candidate_elf" ]; then
            elf_path="$candidate_elf"
            break
        fi
    done

    if [ -z "$elf_path" ]; then
        echo "WARNING: No ELF file found. Build the project first (./manage_device.sh build)."
        echo "         Using placeholder path; update 'executable' in launch.json after building."
        elf_path="build/zephyr/zephyr.elf"
    fi
    echo "   ELF: ${elf_path}"

    # ── 3. Derive OpenOCD target config from BOARD ──────────────────────
    # Map well-known board families to OpenOCD target configs.
    # Falls back to asking the user if the board is not recognized.
    local openocd_target=""
    local openocd_device=""
    case "$BOARD" in
        *nrf52840*)
            openocd_target="target/nrf52.cfg"
            openocd_device="nRF52840_xxAA"
            ;;
        *nrf52832*|*nrf52dk*)
            openocd_target="target/nrf52.cfg"
            openocd_device="nRF52832_xxAA"
            ;;
        *nrf5340*)
            openocd_target="target/nrf5340.cfg"
            openocd_device="nRF5340_xxAA"
            ;;
        *nrf9160*|*nrf91*)
            openocd_target="target/nrf91.cfg"
            openocd_device="nRF9160_xxAA"
            ;;
        *stm32f4*)
            openocd_target="target/stm32f4x.cfg"
            openocd_device="STM32F4"
            ;;
        *stm32f1*)
            openocd_target="target/stm32f1x.cfg"
            openocd_device="STM32F1"
            ;;
        *stm32l4*)
            openocd_target="target/stm32l4x.cfg"
            openocd_device="STM32L4"
            ;;
        *)
            echo "WARNING: Unrecognized BOARD '${BOARD}'. Defaulting to nrf52.cfg."
            echo "         Edit .vscode/launch.json to set the correct OpenOCD target."
            openocd_target="target/nrf52.cfg"
            openocd_device="unknown"
            ;;
    esac
    echo "   OpenOCD target: ${openocd_target}  device: ${openocd_device}"

    # ── 4. Install udev rules if needed ─────────────────────────────────
    local rules_src="60-openocd.rules"
    local rules_dst="/etc/udev/rules.d/60-openocd.rules"
    if [ -f "$rules_src" ] && [ ! -f "$rules_dst" ]; then
        echo "   Installing udev rules (requires sudo)..."
        sudo cp "$rules_src" "$rules_dst"
        sudo udevadm control --reload
        sudo udevadm trigger
        echo "   Udev rules installed."
    elif [ -f "$rules_dst" ]; then
        echo "   Udev rules already installed."
    else
        echo "   No udev rules file found in project — skipping."
    fi

    # ── 5. Kill stale OpenOCD processes ─────────────────────────────────
    if pgrep -x openocd &>/dev/null; then
        echo "   Killing stale OpenOCD processes..."
        pkill -x openocd 2>/dev/null || true
        sleep 0.5
    fi

    # ── 6. Generate .vscode/launch.json ─────────────────────────────────
    mkdir -p .vscode

    cat > .vscode/launch.json <<EOF
{
    "version": "0.2.0",
    "configurations": [
        {
            "name": "Visual Debug (OpenOCD)",
            "cwd": "\${workspaceFolder}",
            "executable": "./${elf_path}",
            "request": "launch",
            "type": "cortex-debug",
            "servertype": "openocd",
            "device": "${openocd_device}",
            "configFiles": [
                "interface/cmsis-dap.cfg",
                "${openocd_target}"
            ],
            "runToEntryPoint": "main",
            "showDevDebugOutput": "raw",
            "gdbPath": "${gdb_path}"
        }
    ]
}
EOF

    echo ""
    echo "   .vscode/launch.json written successfully."
    echo "   Press F5 in VS Code to start debugging."
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
        source ~/zephyrproject/.venv/bin/activate
        eval "west debug -r openocd --no-load ${OPENOCD_ARGS}"
        ;;
    setup-debug)
        setup_debug
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
