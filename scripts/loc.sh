#!/bin/bash
# loc.sh - Count lines of code in SeedOS
#
# Usage: ./scripts/loc.sh [directory]
#
# Counts lines in .c, .h, .S, and .s files, excluding:
#   - limine/ (external bootloader)
#   - build/ (build artifacts)
#
# Shows breakdown by file type and directory.

set -e

ROOT="${1:-.}"
cd "$ROOT"

# File extensions to count
EXTS=("*.c" "*.h" "*.S" "*.s")

# Directories to exclude
EXCLUDES=("limine" "build" ".git")

# Build find exclude args
EXCLUDE_ARGS=""
for dir in "${EXCLUDES[@]}"; do
    EXCLUDE_ARGS="$EXCLUDE_ARGS -path ./$dir -prune -o"
done

count_lines() {
    local pattern="$1"
    find . $EXCLUDE_ARGS -name "$pattern" -print 2>/dev/null | \
        xargs -r wc -l 2>/dev/null | tail -1 | awk '{print $1}'
}

list_files() {
    local pattern="$1"
    find . $EXCLUDE_ARGS -name "$pattern" -print 2>/dev/null
}

echo "=== SeedOS Lines of Code ==="
echo ""

# Count by file type
echo "By file type:"
echo "-------------"
total=0
for ext in "${EXTS[@]}"; do
    files=$(list_files "$ext")
    if [ -n "$files" ]; then
        count=$(echo "$files" | xargs wc -l 2>/dev/null | tail -1 | awk '{print $1}')
        file_count=$(echo "$files" | wc -l)
        printf "  %-8s %6d lines  (%d files)\n" "$ext" "$count" "$file_count"
        total=$((total + count))
    fi
done
echo "-------------"
printf "  %-8s %6d lines\n" "TOTAL" "$total"
echo ""

# Count by directory
echo "By directory:"
echo "-------------"
for dir in arch kernel mm fs drivers lib init include userspace demos; do
    if [ -d "$dir" ]; then
        count=0
        for ext in "${EXTS[@]}"; do
            files=$(find "$dir" -name "$ext" -print 2>/dev/null)
            if [ -n "$files" ]; then
                dir_count=$(echo "$files" | xargs wc -l 2>/dev/null | tail -1 | awk '{print $1}')
                count=$((count + dir_count))
            fi
        done
        if [ "$count" -gt 0 ]; then
            printf "  %-12s %6d lines\n" "$dir/" "$count"
        fi
    fi
done
echo "-------------"
echo ""

# Show top 10 largest files
echo "Top 10 largest files:"
echo "---------------------"
all_files=""
for ext in "${EXTS[@]}"; do
    all_files="$all_files $(list_files "$ext")"
done
echo $all_files | tr ' ' '\n' | grep -v '^$' | \
    xargs wc -l 2>/dev/null | sort -rn | head -11 | tail -10 | \
    while read lines file; do
        printf "  %6d  %s\n" "$lines" "$file"
    done
