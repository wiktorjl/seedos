#!/bin/bash
# mkinitrd.sh - Create ext2 initrd image
#
# Usage: ./scripts/mkinitrd.sh [output_file] [size_mb]
#
# Creates a minimal ext2 filesystem image for testing.
# Uses debugfs to avoid needing root/sudo.

set -e

OUTPUT="${1:-build/initrd.ext2}"
SIZE_MB="${2:-2}"

echo "Creating ${SIZE_MB}MB ext2 image at ${OUTPUT}..."

# Create empty image
dd if=/dev/zero of="$OUTPUT" bs=1M count="$SIZE_MB" 2>/dev/null

# Format as ext2 (mke2fs doesn't need root for regular files)
/sbin/mke2fs -q -t ext2 -F "$OUTPUT"

# Create temp files to write into the filesystem
TMPDIR=$(mktemp -d)
echo "Hello from ext2 initrd!" > "$TMPDIR/hello.txt"

# Create init placeholder
cat > "$TMPDIR/init" << 'EOF'
#!/bin/sh
echo "Init placeholder"
EOF

# Use debugfs to write files (no root needed)
/sbin/debugfs -w "$OUTPUT" << CMDS
mkdir bin
write $TMPDIR/hello.txt hello.txt
write $TMPDIR/init init
CMDS

# Clean up temp files
rm -rf "$TMPDIR"

# Show filesystem info
echo ""
echo "Filesystem contents:"
/sbin/debugfs -R "ls -l /" "$OUTPUT" 2>/dev/null

echo ""
echo "Created $OUTPUT ($(du -h "$OUTPUT" | cut -f1))"
