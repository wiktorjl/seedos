#!/bin/bash
# mkinitrd.sh - Create ext2 initrd image
#
# Usage: ./scripts/mkinitrd.sh [output_file] [size_mb] [init_binary]
#
# Creates a minimal ext2 filesystem image for testing.
# Uses debugfs to avoid needing root/sudo.
#
# If init_binary is provided, it will be copied as /init.
# Otherwise, a placeholder script is created.

set -e

OUTPUT="${1:-build/initrd.ext2}"
SIZE_MB="${2:-2}"
INIT_BINARY="${3:-}"

echo "Creating ${SIZE_MB}MB ext2 image at ${OUTPUT}..."

# Create empty image
dd if=/dev/zero of="$OUTPUT" bs=1M count="$SIZE_MB" 2>/dev/null

# Format as ext2 (mke2fs doesn't need root for regular files)
/sbin/mke2fs -q -t ext2 -F "$OUTPUT"

# Create temp files to write into the filesystem
TMPDIR=$(mktemp -d)
echo "Hello from ext2 initrd!" > "$TMPDIR/hello.txt"

# Create or copy /init
if [ -n "$INIT_BINARY" ] && [ -f "$INIT_BINARY" ]; then
    echo "Using $INIT_BINARY as /init"
    cp "$INIT_BINARY" "$TMPDIR/init"
else
    echo "Creating placeholder /init script"
    cat > "$TMPDIR/init" << 'EOF'
#!/bin/sh
echo "Init placeholder"
EOF
fi

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
