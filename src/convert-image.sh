#!/bin/bash
# Convert image to raw BGRA format for embedding in kernel

set -e

if [ $# -ne 2 ]; then
    echo "Usage: $0 <input.gif> <output.bin>"
    exit 1
fi

INPUT="$1"
OUTPUT="$2"

# Get image dimensions
WIDTH=$(identify -format '%w' "$INPUT")
HEIGHT=$(identify -format '%h' "$INPUT")

echo "Converting $INPUT (${WIDTH}x${HEIGHT}) to raw BGRA..."

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
HEADER="${OUTPUT%.bin}.h"
BASENAME=$(basename "$INPUT" | sed 's/\.[^.]*$//')
echo "Generating $HEADER..."

cat > "$HEADER" << EOF
// Auto-generated from $INPUT
#define ${BASENAME^^}_WIDTH $WIDTH
#define ${BASENAME^^}_HEIGHT $HEIGHT
EOF

echo "Done. Output: $OUTPUT ($((WIDTH * HEIGHT * 4)) bytes)"
