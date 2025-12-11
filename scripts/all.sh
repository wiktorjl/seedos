#!/bin/bash
#
# all.sh - Clean, build, and run the OS
#
# Usage: ./scripts/all.sh [OPTIONS]
#
# This script runs clean, build, and run in sequence.
# Stops immediately if any step fails.
#
# Options are passed through to run.sh:
#   --gui          Run with graphical window (default)
#   --no-gui       Run in terminal-only mode (nographic)
#   --log FILE     Log output to FILE (default: output.log)
#   --no-log       Don't log output to a file
#   -h, --help     Show this help message
#
# Examples:
#   ./scripts/all.sh                    # Clean, build, run with GUI
#   ./scripts/all.sh --no-gui           # Clean, build, run in terminal
#   ./scripts/all.sh --log test.log     # Clean, build, run with custom log
#

set -e  # Exit immediately on any error

# Source common utilities
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

# Handle help flag ourselves
for arg in "$@"; do
    if [[ "$arg" == "-h" || "$arg" == "--help" ]]; then
        echo "Usage: $0 [OPTIONS]"
        echo ""
        echo "Runs clean, build, and run in sequence. Stops on any failure."
        echo ""
        echo "Options (passed to run.sh):"
        echo "  --gui          Run with graphical window (default)"
        echo "  --no-gui       Run in terminal-only mode (nographic)"
        echo "  --log FILE     Log output to FILE (default: output.log)"
        echo "  --no-log       Don't log output to a file"
        echo "  -h, --help     Show this help message"
        exit 0
    fi
done

cd_to_root

echo ""
echo "========================================"
echo "  Seed OS Build Pipeline"
echo "========================================"
echo ""
echo "Steps: clean -> build -> run"
echo ""

# Step 1: Clean
echo ">>> Step 1/3: Clean"
"$SCRIPT_DIR/clean.sh"

# Step 2: Build
echo ""
echo ">>> Step 2/3: Build"
"$SCRIPT_DIR/build.sh"

# Step 3: Run (pass through all arguments)
echo ""
echo ">>> Step 3/3: Run"
"$SCRIPT_DIR/run.sh" "$@"
