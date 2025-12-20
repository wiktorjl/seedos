#!/bin/bash
#
# run.sh - Run the OS in QEMU
#
# Usage: ./scripts/run.sh [OPTIONS]
#
# Options:
#   --gui          Run with graphical window (default)
#   --no-gui       Run in terminal-only mode (nographic)
#   --log FILE     Log output to FILE (default: output.log)
#   --no-log       Don't log output to a file
#   --debug        Start QEMU with GDB server (-s -S), paused for debugger
#   -h, --help     Show this help message
#
# Examples:
#   ./scripts/run.sh                    # GUI mode, log to output.log
#   ./scripts/run.sh --no-gui           # Terminal mode, log to output.log
#   ./scripts/run.sh --log debug.log    # GUI mode, log to debug.log
#   ./scripts/run.sh --no-gui --no-log  # Terminal mode, no logging
#   ./scripts/run.sh --debug            # GUI mode, wait for GDB on localhost:1234
#

set -e

# Source common utilities
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# Directories
BUILD_DIR="build"
LOG_DIR="log"

# Default options
GUI_MODE=true
LOG_FILE="$LOG_DIR/output.log"
DO_LOG=true
DEBUG_MODE=false

# Parse command line arguments
while [[ $# -gt 0 ]]; do
    case "$1" in
        --gui)
            GUI_MODE=true
            shift
            ;;
        --no-gui)
            GUI_MODE=false
            shift
            ;;
        --log)
            if [[ -z "$2" || "$2" == --* ]]; then
                print_error "--log requires a filename argument"
                exit 1
            fi
            LOG_FILE="$2"
            DO_LOG=true
            shift 2
            ;;
        --no-log)
            DO_LOG=false
            shift
            ;;
        --debug)
            DEBUG_MODE=true
            shift
            ;;
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --gui          Run with graphical window (default)"
            echo "  --no-gui       Run in terminal-only mode (nographic)"
            echo "  --log FILE     Log output to FILE (default: output.log)"
            echo "  --no-log       Don't log output to a file"
            echo "  --debug        Start with GDB server (-s -S), paused for debugger"
            echo "  -h, --help     Show this help message"
            exit 0
            ;;
        *)
            print_error "Unknown option: $1"
            echo "Use --help for usage information"
            exit 1
            ;;
    esac
done

cd_to_root

# Check that ISO exists
if [[ ! -f "$BUILD_DIR/seed.iso" ]]; then
    print_error "$BUILD_DIR/seed.iso not found. Run build.sh first."
    exit 1
fi

print_header "RUN"

# Save terminal size to restore later (QEMU can mess it up)
TERM_SIZE=$(stty size 2>/dev/null || echo "")

# Build QEMU command
QEMU_CMD="qemu-system-x86_64 -cdrom $BUILD_DIR/seed.iso"

# Set the CPU to Xeon for better compatibility
# QEMU_CMD="$QEMU_CMD -cpu i7-6700K"

if [[ "$GUI_MODE" == true ]]; then
    echo "Mode: GUI (graphical window)"
    QEMU_CMD="$QEMU_CMD -serial stdio"
else
    echo "Mode: No GUI (terminal only, BIOS)"
    QEMU_CMD="$QEMU_CMD -display none -serial stdio"
fi

if [[ "$DEBUG_MODE" == true ]]; then
    echo "Debug: GDB server on localhost:1234 (QEMU paused)"
    QEMU_CMD="$QEMU_CMD -s -S"
fi

if [[ "$DO_LOG" == true ]]; then
    mkdir -p "$LOG_DIR"
    echo "Logging to: $LOG_FILE"
    QEMU_CMD="$QEMU_CMD 2>&1 | tee $LOG_FILE"
else
    echo "Logging: disabled"
fi

echo ""
echo "Starting QEMU..."
echo "(Press Ctrl+A, X to exit in no-gui mode)"
echo ""

# Run QEMU
eval $QEMU_CMD

reset
stty sane

# Restore terminal size if we saved it
if [[ -n "$TERM_SIZE" ]]; then
    printf '\e[8;%d;%dt' ${TERM_SIZE%% *} ${TERM_SIZE##* }
fi

print_success "QEMU exited"
