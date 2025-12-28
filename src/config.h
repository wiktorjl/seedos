// SeedOS build configuration

#ifndef CONFIG_H
#define CONFIG_H

// Log levels (0 = most severe, 5 = most verbose)
#define LOG_PANIC   0
#define LOG_ERROR   1
#define LOG_WARN    2
#define LOG_INFO    3
#define LOG_DEBUG   4
#define LOG_TRACE   5

// Minimum log level (messages above this level are suppressed)
#define CONFIG_LOG_LEVEL    LOG_TRACE

// Enable/disable output backends
#define CONFIG_OUTPUT_CONSOLE       1
#define CONFIG_OUTPUT_SERIAL        1

// Console colors
#define CONFIG_CONSOLE_COLOR_DEFAULT 0xFFFFFF  // White - user-facing output
#define CONFIG_CONSOLE_COLOR_PANIC   0xFF0000  // Red
#define CONFIG_CONSOLE_COLOR_ERROR   0xFF0000  // Red
#define CONFIG_CONSOLE_COLOR_WARN    0xFFFF00  // Yellow
#define CONFIG_CONSOLE_COLOR_INFO    0x00FF00  // Green - boot progress
#define CONFIG_CONSOLE_COLOR_DEBUG   0x888888  // Gray
#define CONFIG_CONSOLE_COLOR_TRACE   0x666666  // Dark gray

#endif
