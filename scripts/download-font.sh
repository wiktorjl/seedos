#!/bin/bash
# Download and prepare Spleen 8x16 bitmap font for kernel use

set -e

FONT_VERSION="2.1.0"
FONT_URL="https://github.com/fcambus/spleen/releases/download/${FONT_VERSION}/spleen-${FONT_VERSION}.tar.gz"
FONT_FILE="spleen-8x16.psfu"
OUTPUT_FILE="font.bin"

echo "Downloading Spleen ${FONT_VERSION}..."
curl -sL "$FONT_URL" -o /tmp/spleen.tar.gz

echo "Extracting ${FONT_FILE}..."
tar -xzf /tmp/spleen.tar.gz -C /tmp "spleen-${FONT_VERSION}/${FONT_FILE}"

echo "Converting PSF to raw font data..."
# PSF1 format: 4-byte header (magic, mode, charsize), then glyph data
# Skip header, extract 256 glyphs * 16 bytes = 4096 bytes
dd if="/tmp/spleen-${FONT_VERSION}/${FONT_FILE}" of="data/$OUTPUT_FILE" bs=1 skip=4 count=4096 2>/dev/null

echo "Cleaning up..."
rm -rf /tmp/spleen.tar.gz "/tmp/spleen-${FONT_VERSION}"

echo "Done! Created data/${OUTPUT_FILE} ($(wc -c < "data/$OUTPUT_FILE") bytes)"
