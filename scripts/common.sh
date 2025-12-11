#!/bin/bash
#
# common.sh - Shared utilities for build scripts
#
# This file is sourced by other scripts to provide common functionality.
# It handles directory detection so scripts work from os/ or os/scripts/.
#

# Determine project root directory
# Works whether invoked from os/ or os/scripts/
get_project_root() {
    local script_dir="$(cd "$(dirname "${BASH_SOURCE[1]}")" && pwd)"

    if [[ "$(basename "$script_dir")" == "scripts" ]]; then
        # Running from scripts/ or invoked as scripts/foo.sh
        echo "$(dirname "$script_dir")"
    else
        # Running from project root
        echo "$script_dir"
    fi
}

# Change to project root
cd_to_root() {
    local root="$(get_project_root)"
    cd "$root" || exit 1
}

# Print a section header
print_header() {
    echo ""
    echo "========================================"
    echo "  $1"
    echo "========================================"
    echo ""
}

# Print success message
print_success() {
    echo "[OK] $1"
}

# Print error message
print_error() {
    echo "[ERROR] $1" >&2
}
