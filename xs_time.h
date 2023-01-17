/* copyright (c) 2022 - 2023 grunfink / MIT license */

#ifndef _XS_TIME_H

#define _XS_TIME_H

#include <time.h>

d_char *xs_str_time(time_t t, const char *fmt, int local);
#define xs_str_localtime(t, fmt) xs_str_time(t, fmt, 1)
#define xs_str_utctime(t, fmt)   xs_str_time(t, fmt, 0)
time_t xs_parse_time(const char *str, const char *fmt, int local);
#define xs_parse_localtime(str, fmt) xs_parse_time(str, fmt, 1)
#define xs_parse_utctime(str, fmt) xs_parse_time(str, fmt, 0)

#ifdef XS_IMPLEMENTATION

d_char *xs_str_time(time_t t, const char *fmt, int local)
/* returns a d_char with a formated time */
{
    struct tm tm;
    char tmp[64];

    if (t == 0)
        t = time(NULL);

    if (local)
        localtime_r(&t, &tm);
    else
        gmtime_r(&t, &tm);

    strftime(tmp, sizeof(tmp), fmt, &tm);

//    printf("%d %d\n", local, t - xs_parse_time(tmp, fmt, local));

    return xs_str_new(tmp);
}


char *strptime(const char *s, const char *format, struct tm *tm);

time_t xs_parse_time(const char *str, const char *fmt, int local)
{
    struct tm tm;

    memset(&tm, '\0', sizeof(tm));
    strptime(str, fmt, &tm);

    /* try to guess the Daylight Saving Time */
    if (local)
        tm.tm_isdst = -1;

    return local ? mktime(&tm) : timegm(&tm);
}


#endif /* XS_IMPLEMENTATION */

#endif /* _XS_TIME_H */
