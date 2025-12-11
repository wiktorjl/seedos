#!/bin/bash
#
# clean.sh - Remove all build artifacts
#
# Usage: ./scripts/clean.sh
#        (or from scripts/: ./clean.sh)
#

set -e

# Source common utilities
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$SCRIPT_DIR/common.sh"

cd_to_root

print_header "CLEAN"

# Clean everything via Makefile (removes build/ directory)
echo "Removing build directory..."
make clean

# Clean any stray log files in root
echo "Removing log files..."
rm -f *.log

print_success "Clean complete"
