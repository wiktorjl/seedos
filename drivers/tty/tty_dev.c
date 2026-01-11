// SPDX-License-Identifier: GPL-2.0-only
/*
 * TTY Device Implementation
 *
 * Implements VFS file operations for the console device. Reads come from
 * the keyboard driver, writes go to the terminal.
 */

#include "tty_dev.h"
#include "terminal.h"
#include "keyboard.h"
#include "heap.h"
#include "log.h"

/*
 * Forward declarations for file operations
 */
static ssize_t tty_read(vfs_file_t *file, void *buf, size_t count);
static ssize_t tty_write(vfs_file_t *file, const void *buf, size_t count);
static int tty_close(vfs_file_t *file);

/*
 * TTY file operations structure
 */
static file_ops_t tty_ops = {
    .read  = tty_read,
    .write = tty_write,
    .lseek = NULL,  /* TTY is not seekable */
    .close = tty_close,
    .ioctl = NULL,  /* TODO: Add termios support */
};

/**
 * tty_init - Initialize the TTY device subsystem
 */
void tty_init(void)
{
    log_debug("TTY: Device subsystem initialized");
}

/**
 * tty_get_ops - Get the TTY file operations structure
 */
file_ops_t *tty_get_ops(void)
{
    return &tty_ops;
}

/**
 * tty_open - Open a VFS file handle for the console
 */
vfs_file_t *tty_open(int flags)
{
    vfs_file_t *file = vfs_file_alloc();
    if (!file) {
        return NULL;
    }

    file->type = VFS_TYPE_CHR;
    file->flags = flags;
    file->ops = &tty_ops;
    file->private = terminal_get_active();  /* Store terminal pointer */

    return file;
}

/**
 * tty_read - Read from the keyboard
 * @file: Open file handle
 * @buf: Buffer to read into
 * @count: Maximum bytes to read
 *
 * Reads keyboard input. Currently blocking - waits for at least one character.
 * Returns number of bytes read.
 */
static ssize_t tty_read(vfs_file_t *file, void *buf, size_t count)
{
    (void)file;  /* Terminal pointer in private not needed for keyboard */

    if (!buf || count == 0) {
        return 0;
    }

    char *cbuf = (char *)buf;
    size_t bytes_read = 0;

    /* Read at least one character (blocking) */
    cbuf[bytes_read++] = keyboard_read();

    /* Then read any additional available characters (non-blocking) */
    while (bytes_read < count && keyboard_has_input()) {
        int ch = keyboard_getchar();
        if (ch < 0) {
            break;
        }
        cbuf[bytes_read++] = (char)ch;
    }

    return (ssize_t)bytes_read;
}

/**
 * tty_write - Write to the terminal
 * @file: Open file handle
 * @buf: Buffer to write from
 * @count: Bytes to write
 *
 * Writes to the terminal associated with this file handle.
 * Returns number of bytes written.
 */
static ssize_t tty_write(vfs_file_t *file, const void *buf, size_t count)
{
    terminal_t *term = (terminal_t *)file->private;
    if (!term) {
        term = terminal_get_active();
    }

    const char *cbuf = (const char *)buf;

    for (size_t i = 0; i < count; i++) {
        terminal_putchar(term, cbuf[i]);
    }

    return (ssize_t)count;
}

/**
 * tty_close - Close the TTY file handle
 * @file: File to close
 *
 * TTY doesn't need special cleanup - just returns success.
 */
static int tty_close(vfs_file_t *file)
{
    (void)file;
    return 0;
}
