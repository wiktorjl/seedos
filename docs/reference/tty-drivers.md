# SeedOS TTY Drivers Subsystem

## Architecture

```
kprintf -> terminal -> console (framebuffer)
                   \-> serial (COM1)
```

## Console Driver

Features:
- 8x16 pixel bitmap font (Spleen)
- 1000-line scrollback buffer
- Blinking cursor (500ms toggle)
- Page Up/Down navigation
- Fullscreen mode for demos

### API

```c
void console_init(struct limine_framebuffer *fb);
void console_putchar(char c, uint32_t color);
void console_clear(uint32_t color);
void console_scroll_back(int lines);
void console_draw_char(char c, int x, int y, uint32_t color);
void console_fill_rect(int x, int y, int w, int h, uint32_t color);
```

## Serial Driver

COM1 at 115200 baud, 8N1:

```c
void serial_init(void);
void serial_putchar(char c);
void serial_puts(const char *str);
```

## Terminal Abstraction

```c
typedef struct terminal {
    uint32_t backends;  // TERM_BACKEND_FB | TERM_BACKEND_SERIAL
    uint32_t color;
} terminal_t;

void terminal_putchar(terminal_t *term, char c);
void terminal_clear(terminal_t *term);
```

## Output Flow

1. `kprintf()` -> `tkvprintf()` with active terminal
2. `tkvprintf()` parses format, calls `terminal_putchar()`
3. `terminal_putchar()` routes to enabled backends
4. `console_putchar()` updates scrollback and renders
5. `serial_putchar()` writes to COM1

## Configuration

```c
#define CONFIG_OUTPUT_CONSOLE 1
#define CONFIG_OUTPUT_SERIAL  1
#define CONFIG_LOG_LEVEL LOG_TRACE
```
