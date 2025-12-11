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
#   -h, --help     Show this help message
#
# Examples:
#   ./scripts/run.sh                    # GUI mode, log to output.log
#   ./scripts/run.sh --no-gui           # Terminal mode, log to output.log
#   ./scripts/run.sh --log debug.log    # GUI mode, log to debug.log
#   ./scripts/run.sh --no-gui --no-log  # Terminal mode, no logging
#

set -e

# Source common utilities
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# Default options
GUI_MODE=true
LOG_FILE="output.log"
DO_LOG=true

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
        -h|--help)
            echo "Usage: $0 [OPTIONS]"
            echo ""
            echo "Options:"
            echo "  --gui          Run with graphical window (default)"
            echo "  --no-gui       Run in terminal-only mode (nographic)"
            echo "  --log FILE     Log output to FILE (default: output.log)"
            echo "  --no-log       Don't log output to a file"
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
if [[ ! -f "myos.iso" ]]; then
    print_error "myos.iso not found. Run build.sh first."
    exit 1
fi

print_header "RUN"

# Save terminal size to restore later (QEMU can mess it up)
TERM_SIZE=$(stty size 2>/dev/null || echo "")

# Build QEMU command
QEMU_CMD="qemu-system-x86_64 -cdrom myos.iso"

if [[ "$GUI_MODE" == true ]]; then
    echo "Mode: GUI (graphical window)"
    QEMU_CMD="$QEMU_CMD -serial stdio"
else
    echo "Mode: No GUI (terminal only, UEFI)"
    QEMU_CMD="$QEMU_CMD -nographic -serial mon:stdio -bios /usr/share/ovmf/OVMF.fd"
fi

if [[ "$DO_LOG" == true ]]; then
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

# Restore terminal size if we saved it
if [[ -n "$TERM_SIZE" ]]; then
    printf '\e[8;%d;%dt' ${TERM_SIZE%% *} ${TERM_SIZE##* }
fi

print_success "QEMU exited"
