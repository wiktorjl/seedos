// Logging interface with level filtering

#ifndef LOG_H
#define LOG_H

#include "config.h"

void log_init(void);

void log_panic(const char *msg);
void log_error(const char *msg);
void log_warn(const char *msg);
void log_info(const char *msg);
void log_debug(const char *msg);
void log_trace(const char *msg);

#endif
