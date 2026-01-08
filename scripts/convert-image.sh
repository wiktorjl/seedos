#!/bin/bash
# Convert image to raw BGRA format for embedding in kernel
# Outputs: build/logo.bin (binary) and src/logo.h (dimensions header)

set -e

if [ $# -ne 1 ]; then
    echo "Usage: $0 <input-image>"
    exit 1
fi

INPUT="$1"
OUTPUT="build/logo.bin"
HEADER="lib/logo.h"

# Get image dimensions
WIDTH=$(identify -format '%w' "$INPUT")
HEIGHT=$(identify -format '%h' "$INPUT")

echo "Converting $INPUT (${WIDTH}x${HEIGHT}) to raw BGRA..."

# Ensure build directory exists
mkdir -p build

# Scale down to fit in ~1MB (target ~512x280 for 16:9)
MAX_PIXELS=150000
PIXELS=$((WIDTH * HEIGHT))

if [ $PIXELS -gt $MAX_PIXELS ]; then
    SCALE=$(echo "scale=2; sqrt($MAX_PIXELS / $PIXELS) * 100" | bc)
    SCALED_W=$(echo "$WIDTH * $SCALE / 100" | bc | cut -d. -f1)
    SCALED_H=$(echo "$HEIGHT * $SCALE / 100" | bc | cut -d. -f1)
    echo "Scaling to ${SCALED_W}x${SCALED_H} to fit memory..."
    convert "$INPUT" -resize ${SCALED_W}x${SCALED_H} -depth 8 BGRA:"$OUTPUT"
    WIDTH=$SCALED_W
    HEIGHT=$SCALED_H
else
    convert "$INPUT" -depth 8 BGRA:"$OUTPUT"
fi

# Generate header with dimensions
echo "Generating $HEADER..."

cat > "$HEADER" << EOF
/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Logo display
 *
 * Regenerate image data with: scripts/convert-image.sh
 */

#ifndef _LOGO_H
#define _LOGO_H

#include "types.h"

#define LOGO_WIDTH $WIDTH
#define LOGO_HEIGHT $HEIGHT

/* Raw BGRA pixel data embedded via .incbin in boot.S */
extern uint32_t logo_data[];

/**
 * logo_display - Display the SeedOS logo on the console
 */
void logo_display(void);

#endif /* _LOGO_H */
EOF

echo "Done. Output: $OUTPUT ($((WIDTH * HEIGHT * 4)) bytes)"
