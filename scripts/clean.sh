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

# Clean compiled objects and kernel
echo "Removing object files and kernel..."
make clean

# Clean ISO artifacts
echo "Removing ISO build artifacts..."
rm -rf iso_root
rm -f seed.iso

# Clean log files
echo "Removing log files..."
rm -f *.log

print_success "Clean complete"
