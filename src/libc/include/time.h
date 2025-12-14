/*
 * time.h - Time Types and Functions
 */

#ifndef _TIME_H
#define _TIME_H

#include <sys/types.h>

struct tm {
    int tm_sec;     /* Seconds (0-60) */
    int tm_min;     /* Minutes (0-59) */
    int tm_hour;    /* Hours (0-23) */
    int tm_mday;    /* Day of month (1-31) */
    int tm_mon;     /* Month (0-11) */
    int tm_year;    /* Year - 1900 */
    int tm_wday;    /* Day of week (0-6, Sunday = 0) */
    int tm_yday;    /* Day in year (0-365) */
    int tm_isdst;   /* Daylight saving time flag */
};

/* Stub functions - these don't do much in SeedOS */
time_t time(time_t *tloc);
struct tm *localtime(const time_t *timep);
char *ctime(const time_t *timep);

#endif /* _TIME_H */
