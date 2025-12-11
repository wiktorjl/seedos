# Serial Port Communication (UART 16550)

This document explains serial port communication from first principles, surveys how major operating systems use serial ports, and provides a detailed walkthrough of our UART driver implementation.

## Table of Contents

1. [What is a Serial Port?](#what-is-a-serial-port)
2. [Why Serial Ports Matter for OS Development](#why-serial-ports-matter-for-os-development)
3. [The UART 16550](#the-uart-16550)
4. [How Major Systems Use Serial Ports](#how-major-systems-use-serial-ports)
5. [Our Design Decisions](#our-design-decisions)
6. [Implementation Walkthrough](#implementation-walkthrough)
7. [Using Serial Output with QEMU](#using-serial-output-with-qemu)

---

## What is a Serial Port?

A **serial port** transmits data one bit at a time over a single wire (plus ground). This is in contrast to **parallel** communication, which sends multiple bits simultaneously over multiple wires.

### Serial vs Parallel

```
Serial (1 bit at a time):
                         Time →
Data wire:  ─┬─┬─┬─┬─┬─┬─┬─┬─
             │1│0│1│1│0│0│1│0│  = 0xB2 (one byte, 8 clock cycles)
             └─┴─┴─┴─┴─┴─┴─┴─┘

Parallel (8 bits at a time):
Wire 0: ─┬─┬─
         │0│1│
Wire 1: ─┼─┼─
         │1│0│
Wire 2: ─┼─┼─      = 0xB2, 0x47 (two bytes, 2 clock cycles)
         │0│0│
  ...    ...
Wire 7: ─┼─┼─
         │1│0│
         └─┴─┘
```

**Why use serial?**
- Fewer wires needed (cheaper cables, smaller connectors)
- Works over longer distances (less signal degradation)
- Simpler timing (no skew between parallel wires)
- Universal standard (RS-232) that everything supports

### RS-232 Standard

The traditional PC serial port follows the **RS-232** standard, which defines:

- **Voltage levels**: +3 to +15V for logical 0, -3 to -15V for logical 1
- **Connector**: DB-9 or DB-25 pin connector
- **Signals**: Data (TX/RX), handshaking (RTS/CTS, DTR/DSR), ground

```
DB-9 Connector (DTE - PC side):

    1  2  3  4  5
     ●  ●  ●  ●  ●
      ●  ●  ●  ●
      6  7  8  9

Pin assignments:
  1 - DCD (Data Carrier Detect)
  2 - RXD (Receive Data)        ← Data IN to PC
  3 - TXD (Transmit Data)       → Data OUT from PC
  4 - DTR (Data Terminal Ready) → PC is ready
  5 - GND (Ground)
  6 - DSR (Data Set Ready)      ← Device is ready
  7 - RTS (Request To Send)     → PC wants to send
  8 - CTS (Clear To Send)       ← Device ready to receive
  9 - RI  (Ring Indicator)      ← Incoming call (modem)
```

### Communication Parameters

Serial communication requires both sides to agree on:

1. **Baud rate**: Bits per second (e.g., 9600, 38400, 115200)
2. **Data bits**: Usually 7 or 8 bits per character
3. **Parity**: Error checking (None, Even, Odd)
4. **Stop bits**: 1, 1.5, or 2 bits between characters

Common notation: **8N1** = 8 data bits, No parity, 1 stop bit

```
One character transmission at 8N1:

    ┌─────┐ ┌─┬─┬─┬─┬─┬─┬─┬─┐ ┌─────┐
    │Start│ │0│1│2│3│4│5│6│7│ │Stop │
    │  0  │ │ Data bits (LSB │ │  1  │
    └─────┘ │    first)      │ └─────┘
            └─┴─┴─┴─┴─┴─┴─┴─┘

    Start bit: Always 0 (signals start of character)
    Data bits: The actual character (e.g., 'A' = 0x41)
    Stop bit:  Always 1 (allows receiver to resync)
```

---

## Why Serial Ports Matter for OS Development

Serial ports are invaluable for kernel development:

### 1. Always Available

The serial port works from the moment the CPU starts executing. Unlike:
- **Framebuffer**: Needs bootloader setup
- **USB**: Requires complex driver stack
- **Network**: Needs full TCP/IP implementation

### 2. Works Without Graphics

If your graphics driver crashes, you can still see serial output. This is essential for debugging early boot and graphics initialization.

### 3. Easy to Log and Search

Serial output goes to your terminal, where you can:
- Scroll through history
- Search for specific messages (`grep`)
- Redirect to files
- Copy/paste easily

### 4. Machine-Readable

Unlike graphical output, serial text can be processed by scripts for automated testing.

### 5. Works with Physical Hardware

When running on real hardware (not just QEMU), a serial cable connected to another computer gives you a debugging console.

---

## The UART 16550

The **Universal Asynchronous Receiver/Transmitter (UART)** is the chip that handles serial communication. The **16550** is the standard PC UART, introduced in 1987 and still emulated in modern systems.

### UART Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                        UART 16550                               │
│                                                                 │
│  ┌─────────────┐     ┌─────────────┐     ┌─────────────┐       │
│  │   TX FIFO   │────►│  TX Shift   │────►│    TXD     │──────► │
│  │  (16 bytes) │     │  Register   │     │   (pin)    │  Data  │
│  └─────────────┘     └─────────────┘     └─────────────┘  Out   │
│        ▲                                                        │
│        │ CPU writes                                             │
│        │                                                        │
│  ┌─────────────┐     ┌─────────────┐     ┌─────────────┐       │
│  │   RX FIFO   │◄────│  RX Shift   │◄────│    RXD     │◄────── │
│  │  (16 bytes) │     │  Register   │     │   (pin)    │  Data  │
│  └─────────────┘     └─────────────┘     └─────────────┘   In   │
│        │                                                        │
│        ▼ CPU reads                                              │
│                                                                 │
│  ┌─────────────────────────────────────────────────────────┐   │
│  │                    Control Registers                     │   │
│  │  • Line Control (data format)                           │   │
│  │  • FIFO Control (enable/disable FIFOs)                  │   │
│  │  • Modem Control (RTS, DTR signals)                     │   │
│  │  • Line Status (TX empty, RX ready, errors)             │   │
│  │  • Divisor Latch (baud rate)                            │   │
│  └─────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────┘
```

### The 16550's FIFOs

The "16550" in the name refers to the 16-byte FIFOs (First-In-First-Out buffers):

- **Without FIFO** (8250 UART): CPU must handle each byte immediately
- **With FIFO** (16550): Up to 16 bytes can buffer, reducing interrupt overhead

```
Without FIFO (8250):                With FIFO (16550):

Byte arrives → IRQ                  Bytes arrive → buffer
CPU handles   → IRQ                              → buffer
Byte arrives → IRQ                              → buffer
CPU handles   → IRQ                              → buffer
  ...                                            ...
(interrupt per byte)                IRQ when threshold reached
                                    CPU handles all at once
```

### Register Map

The UART is accessed through I/O ports. COM1 is at base address **0x3F8**:

| Offset | DLAB=0 Read | DLAB=0 Write | DLAB=1 Read/Write |
|--------|-------------|--------------|-------------------|
| +0 | RBR (Receive Buffer) | THR (Transmit Hold) | DLL (Divisor Low) |
| +1 | IER (Interrupt Enable) | IER | DLH (Divisor High) |
| +2 | IIR (Interrupt ID) | FCR (FIFO Control) | - |
| +3 | LCR (Line Control) | LCR | - |
| +4 | MCR (Modem Control) | MCR | - |
| +5 | LSR (Line Status) | - | - |
| +6 | MSR (Modem Status) | - | - |
| +7 | Scratch Register | Scratch Register | - |

**DLAB** (Divisor Latch Access Bit) in the Line Control Register switches what registers +0 and +1 access. This is a common hardware trick to fit more registers into limited I/O space.

### Key Registers Explained

#### Line Control Register (LCR) - Offset +3

```
Bit 7    Bit 6    Bit 5    Bit 4    Bit 3    Bit 2    Bit 1    Bit 0
┌────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┐
│  DLAB  │ Break  │ Stick  │  EPS   │  PEN   │  STB   │  WLS1  │  WLS0  │
└────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┘

DLAB:  Divisor Latch Access Bit (1 = access divisor registers)
Break: Set break condition
Stick: Stick parity
EPS:   Even Parity Select
PEN:   Parity Enable
STB:   Stop bits (0 = 1 stop bit, 1 = 2 stop bits)
WLS:   Word Length Select (00=5, 01=6, 10=7, 11=8 bits)

For 8N1: LCR = 0x03 (8 data bits, no parity, 1 stop bit, DLAB=0)
```

#### Line Status Register (LSR) - Offset +5

```
Bit 7    Bit 6    Bit 5    Bit 4    Bit 3    Bit 2    Bit 1    Bit 0
┌────────┬────────┬────────┬────────┬────────┬────────┬────────┬────────┐
│  FIFO  │  TEMT  │  THRE  │   BI   │   FE   │   PE   │   OE   │   DR   │
│  Err   │        │        │        │        │        │        │        │
└────────┴────────┴────────┴────────┴────────┴────────┴────────┴────────┘

DR:   Data Ready (1 = data available to read)
OE:   Overrun Error (data lost)
PE:   Parity Error
FE:   Framing Error
BI:   Break Interrupt
THRE: Transmit Holding Register Empty (1 = can write)  ← We poll this!
TEMT: Transmitter Empty (shift register empty too)
```

#### Baud Rate Divisor

The baud rate is set by a 16-bit divisor:

```
Divisor = 115200 / desired_baud_rate

Examples:
  115200 baud: divisor = 1
  57600 baud:  divisor = 2
  38400 baud:  divisor = 3
  19200 baud:  divisor = 6
  9600 baud:   divisor = 12
```

To set the divisor:
1. Set DLAB=1 in LCR
2. Write low byte to offset +0
3. Write high byte to offset +1
4. Clear DLAB=0 in LCR

---

## How Major Systems Use Serial Ports

### Linux

Linux has extensive serial support for both debugging and general use:

**Kernel Console:**
- Boot parameter `console=ttyS0,115200n8` redirects kernel messages to serial
- `earlycon=uart,io,0x3f8,115200n8` enables output before the main driver loads
- Essential for debugging headless servers and embedded systems

**Driver Architecture:**
- `drivers/tty/serial/8250/` - 16550 UART driver
- Uses TTY subsystem for line discipline, job control
- Supports both polling and interrupt-driven modes
- DMA support for high throughput

**KGDB:**
- Kernel debugger can use serial port for GDB connection
- Allows breakpoints, single-stepping kernel code
- Boot with `kgdboc=ttyS0,115200`

### macOS / Darwin

**Serial Console:**
- Less commonly used (Macs rarely have serial ports)
- `nvram boot-args="debug=0x14e serial=3"` enables serial output
- Uses Thunderbolt-to-serial adapters on modern hardware

**Driver:**
- IOSerialFamily framework in I/O Kit
- `AppleUSBUART.kext` for USB serial adapters

### Windows

**Kernel Debugging:**
- Boot Configuration Data (BCD): `bcdedit /debug on`, `bcdedit /dbgsettings serial`
- WinDbg connects over serial for kernel debugging
- Blue Screen of Death (BSOD) info can be sent to serial

**Driver:**
- `serial.sys` - Main serial port driver
- Part of the Windows Driver Model (WDM)
- Full modem and flow control support

### Comparison

| Feature | Linux | macOS | Windows |
|---------|-------|-------|---------|
| Early boot console | Yes (earlycon) | Limited | Limited |
| Kernel debugger | KGDB | lldb (limited) | WinDbg |
| Driver location | 8250/ | IOSerialFamily | serial.sys |
| Primary use case | Servers, embedded | Development | Debugging |

---

## Our Design Decisions

For Seed OS, we use serial output primarily for debugging. Our design prioritizes simplicity:

### Design Goals

1. **Work immediately** - Available before any other subsystem
2. **Simple polling** - No interrupt complexity
3. **Reliable** - Never lose output due to buffer issues
4. **Educational** - Show exactly what's happening

### Key Decisions

**1. Polling, Not Interrupts**

We busy-wait for the transmitter to be ready:

```c
while (!serial_is_transmit_ready())
    ;  // Spin until THRE bit is set
outb(SERIAL_DATA, c);
```

Real drivers use interrupts to avoid wasting CPU cycles, but polling is:
- Simpler to implement
- Easier to understand
- Works in any context (including early boot, interrupt handlers)
- Fast enough for debugging output

**2. Fixed Configuration: 38400 8N1**

We hardcode the baud rate and format. A real driver would:
- Auto-detect or configure via device tree
- Support runtime baud rate changes
- Handle different data formats

38400 baud is a good balance:
- Fast enough for reasonable throughput
- Slow enough to be reliable
- Standard setting that QEMU defaults to

**3. No Hardware Flow Control**

We ignore RTS/CTS signals. This means:
- We might overflow slow receivers
- Works fine for QEMU and fast terminals
- Simplifies the implementation

**4. CR+LF Line Endings**

Serial terminals traditionally expect `\r\n` (Carriage Return + Line Feed), not just `\n`. We convert automatically:

```c
if (*s == '\n')
    serial_putc('\r');  // Add CR before LF
serial_putc(*s);
```

---

## Implementation Walkthrough

Our serial driver lives in `kernel.c`. Let's trace through each part.

### I/O Port Definitions (kernel.c:80-105)

```c
/* COM1 base I/O port address */
#define SERIAL_COM1_BASE     0x3F8

/* Register offsets from base address */
#define SERIAL_DATA          (SERIAL_COM1_BASE + 0)  /* Data / Divisor low */
#define SERIAL_INTR_ENABLE   (SERIAL_COM1_BASE + 1)  /* Int enable / Divisor high */
#define SERIAL_FIFO_CTRL     (SERIAL_COM1_BASE + 2)  /* FIFO control */
#define SERIAL_LINE_CTRL     (SERIAL_COM1_BASE + 3)  /* Line control */
#define SERIAL_MODEM_CTRL    (SERIAL_COM1_BASE + 4)  /* Modem control */
#define SERIAL_LINE_STATUS   (SERIAL_COM1_BASE + 5)  /* Line status */
```

Standard PC COM port addresses:
- COM1: 0x3F8 (IRQ 4)
- COM2: 0x2F8 (IRQ 3)
- COM3: 0x3E8 (IRQ 4)
- COM4: 0x2E8 (IRQ 3)

### Configuration Constants (kernel.c:91-105)

```c
/* Line Control Register bits */
#define SERIAL_LCR_DLAB      0x80     /* Divisor Latch Access Bit */
#define SERIAL_LCR_8N1       0x03     /* 8 data bits, No parity, 1 stop */

/* FIFO Control Register bits */
#define SERIAL_FCR_ENABLE    0xC7     /* Enable FIFO, clear, 14-byte threshold */

/* Modem Control Register bits */
#define SERIAL_MCR_RTS_DTR   0x03     /* RTS and DTR active */

/* Baud rate divisor for 38400 baud */
#define SERIAL_DIVISOR_38400 0x03     /* 115200 / 38400 = 3 */

/* Line Status Register - transmitter empty bit */
#define SERIAL_LSR_TX_EMPTY  0x20
```

### Initialization (kernel.c:118-126)

```c
static void serial_init(void) {
    outb(SERIAL_INTR_ENABLE, 0x00);              /* Disable interrupts */
    outb(SERIAL_LINE_CTRL,   SERIAL_LCR_DLAB);   /* Enable DLAB */
    outb(SERIAL_DATA,        SERIAL_DIVISOR_38400); /* Divisor low byte (3) */
    outb(SERIAL_INTR_ENABLE, 0x00);              /* Divisor high byte (0) */
    outb(SERIAL_LINE_CTRL,   SERIAL_LCR_8N1);    /* 8N1, clear DLAB */
    outb(SERIAL_FIFO_CTRL,   SERIAL_FCR_ENABLE); /* Enable FIFOs */
    outb(SERIAL_MODEM_CTRL,  SERIAL_MCR_RTS_DTR);/* Assert RTS and DTR */
}
```

Step by step:

1. **Disable interrupts** (offset +1 = 0x00)
   - We're polling, not using interrupts

2. **Set DLAB=1** (offset +3 = 0x80)
   - Switches offsets +0 and +1 to divisor registers

3. **Set divisor = 3** (offsets +0 and +1)
   - Low byte: 3
   - High byte: 0
   - 115200 / 3 = 38400 baud

4. **Set 8N1, clear DLAB** (offset +3 = 0x03)
   - Bits 0-1 = 0b11 = 8 data bits
   - Bit 2 = 0 = 1 stop bit
   - Bits 3-5 = 0 = no parity
   - Bit 7 = 0 = DLAB off (back to normal mode)

5. **Enable FIFOs** (offset +2 = 0xC7)
   - Bit 0 = 1: Enable FIFOs
   - Bits 1-2 = 1: Clear TX and RX FIFOs
   - Bits 6-7 = 11: 14-byte trigger threshold

6. **Set modem control** (offset +4 = 0x03)
   - Bit 0 = 1: DTR (Data Terminal Ready)
   - Bit 1 = 1: RTS (Request To Send)
   - These signals tell connected devices we're ready

### Checking Transmit Ready (kernel.c:133-135)

```c
static int serial_is_transmit_ready(void) {
    return inb(SERIAL_LINE_STATUS) & SERIAL_LSR_TX_EMPTY;
}
```

Reads the Line Status Register and checks bit 5 (THRE - Transmit Holding Register Empty). Non-zero means we can write another byte.

### Sending a Character (kernel.c:143-147)

```c
void serial_putc(char c) {
    while (!serial_is_transmit_ready())
        ;  /* Busy-wait until ready */
    outb(SERIAL_DATA, c);
}
```

This is the core transmit function:
1. Spin until the transmitter is ready
2. Write the character to the data register

**Why busy-wait?** In a real OS, you'd use interrupts to avoid wasting CPU. But for debugging output:
- It's simpler
- It's synchronous (output appears immediately)
- The wait is typically very short (UART is fast)

### Sending a String (kernel.c:155-162)

```c
void serial_puts(const char *s) {
    while (*s) {
        if (*s == '\n')
            serial_putc('\r');  /* CR before LF */
        serial_putc(*s);
        s++;
    }
}
```

The `\n` to `\r\n` conversion is important:
- Unix uses `\n` (LF) for newlines
- Serial terminals expect `\r\n` (CR+LF)
- Without CR, each line would start where the previous ended

### Printing Hex Values (kernel.c:177-183)

```c
void serial_put_hex(uint64_t value) {
    const char *hex_digits = "0123456789abcdef";
    serial_puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4) {
        serial_putc(hex_digits[(value >> shift) & 0xF]);
    }
}
```

Extracts each 4-bit nibble from most-significant to least:
- Shift 60: bits 63-60 (first hex digit)
- Shift 56: bits 59-56 (second hex digit)
- ... and so on for all 16 hex digits

The `& 0xF` masks to 4 bits, giving a value 0-15 that indexes into `hex_digits`.

### Printing Decimal Values (kernel.c:196-211)

```c
void serial_put_dec(uint64_t value) {
    if (value == 0) {
        serial_putc('0');
        return;
    }
    char buf[21];  /* Max 20 digits for 64-bit int */
    int i = 0;
    while (value > 0) {
        buf[i++] = '0' + (value % 10);
        value /= 10;
    }
    while (i > 0) {
        serial_putc(buf[--i]);
    }
}
```

Algorithm:
1. Handle zero specially (avoid empty output)
2. Extract digits from least significant to most using `% 10`
3. Store in buffer (reversed order)
4. Output buffer in reverse (correct order)

---

## Using Serial Output with QEMU

### Basic Usage

```bash
# Output to terminal (stdio)
qemu-system-x86_64 -cdrom myos.iso -serial stdio

# Output to file
qemu-system-x86_64 -cdrom myos.iso -serial file:serial.log

# Output to pseudo-terminal (for minicom/screen)
qemu-system-x86_64 -cdrom myos.iso -serial pty
```

### Combining with Display

```bash
# Serial to terminal, graphics in window
qemu-system-x86_64 -cdrom myos.iso -serial stdio

# Serial to terminal, no graphics window
qemu-system-x86_64 -cdrom myos.iso -serial stdio -nographic

# Monitor and serial multiplexed on stdio
qemu-system-x86_64 -cdrom myos.iso -serial mon:stdio
```

With `-serial mon:stdio`, you can switch between serial output and QEMU monitor:
- `Ctrl-A C` - Switch to QEMU monitor
- `Ctrl-A C` again - Switch back to serial
- `Ctrl-A X` - Exit QEMU

### Debugging Tips

**1. Add timestamps:**
```c
void serial_log(const char *msg) {
    serial_puts("[");
    serial_put_dec(get_ticks());  // If you have a timer
    serial_puts("] ");
    serial_puts(msg);
}
```

**2. Log to file for analysis:**
```bash
qemu-system-x86_64 -cdrom myos.iso -serial file:boot.log
# Later:
grep "ERROR" boot.log
```

**3. Different log levels:**
```c
#define LOG_DEBUG 0
#define LOG_INFO  1
#define LOG_ERROR 2

int log_level = LOG_INFO;

void log(int level, const char *msg) {
    if (level >= log_level) {
        serial_puts(msg);
    }
}
```

---

## Complete Data Flow

```
┌─────────────────────────────────────────────────────────────────────────────┐
│  Kernel code calls puts("Hello\n")                                          │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  serial_puts("Hello\n") in kernel.c                                         │
│  • Loop through each character                                              │
│  • Convert \n to \r\n                                                       │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  serial_putc('H') ... serial_putc('\r') serial_putc('\n')                   │
│  • For each character:                                                      │
│    1. Poll LSR bit 5 until transmitter ready                                │
│    2. Write character to THR (port 0x3F8)                                   │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  UART 16550 (emulated by QEMU)                                              │
│  • Receives byte in Transmit Holding Register                               │
│  • Shifts bits out on TX line at configured baud rate                       │
│  • Sets THRE when ready for next byte                                       │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  QEMU serial backend                                                        │
│  • -serial stdio: writes to host terminal                                   │
│  • -serial file:X: writes to file X                                         │
│  • -serial pty: creates pseudo-terminal                                     │
└─────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────────┐
│  Host terminal displays "Hello" and moves to new line                       │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Key Files Summary

| File | Purpose |
|------|---------|
| `kernel.c` | Serial driver implementation (serial_init, serial_putc, serial_puts) |
| `io.h` | Low-level I/O port access (inb, outb) |
| `console.c` | Unified output (uses serial_puts for dual output) |

---

## Further Reading

- [OSDev Wiki: Serial Ports](https://wiki.osdev.org/Serial_Ports)
- [16550 UART Datasheet](https://www.ti.com/lit/ds/symlink/tl16c550c.pdf)
- [RS-232 Standard](https://en.wikipedia.org/wiki/RS-232)
- [QEMU Serial Port Documentation](https://www.qemu.org/docs/master/system/invocation.html#hxtool-5)

---

## Exercises

1. **Add serial input**: Implement `serial_getc()` that reads from the receive buffer. Poll the DR (Data Ready) bit in LSR.

2. **Interrupt-driven TX**: Modify the driver to use interrupts instead of polling. Enable the THRE interrupt in IER.

3. **Different baud rates**: Add a `serial_set_baud(uint32_t baud)` function that reconfigures the divisor.

4. **Implement printf**: Build a `serial_printf()` function with format string support (%d, %x, %s).

5. **Add a second COM port**: Initialize COM2 at 0x2F8 and provide functions to write to either port.
