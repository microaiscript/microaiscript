/*
 * AiScript Standard Library - Date Namespace Implementation
 */

#include "std_date.h"
#include "types.h"
#include "scope.h"
#include "memory.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L  /* Enable localtime_r, gmtime_r */
#endif
#include <time.h>

/* Platform-specific includes for high-resolution time */
#ifdef _WIN32
    #include <windows.h>
    #include <sys/timeb.h>
#else
    #include <sys/time.h>
#endif

#ifndef NO_DATE

/* Startup time for embedded systems without RTC */
#ifdef DATE_USE_STARTUP_TIME
static double startup_time_ms = 0;
static bool startup_time_initialized = false;
#endif

/* Helper: Get current time in milliseconds since Unix epoch */
static double get_current_time_ms(void) {
    #ifdef DATE_USE_STARTUP_TIME
        /* For embedded systems: return time since program start */
        if (!startup_time_initialized) {
            startup_time_ms = 0;
            startup_time_initialized = true;
        }
        #ifdef _WIN32
            return (double)GetTickCount64();
        #else
            struct timespec ts;
            clock_gettime(CLOCK_MONOTONIC, &ts);
            return (ts.tv_sec * 1000.0) + (ts.tv_nsec / 1000000.0);
        #endif
    #else
        /* Normal systems: get actual wall clock time */
        #ifdef _WIN32
            struct _timeb timebuffer;
            _ftime(&timebuffer);
            return (timebuffer.time * 1000.0) + timebuffer.millitm;
        #else
            struct timeval tv;
            gettimeofday(&tv, NULL);
            return (tv.tv_sec * 1000.0) + (tv.tv_usec / 1000.0);
        #endif
    #endif
}

/* Helper: Convert milliseconds to struct tm */
static void ms_to_tm(double ms, struct tm* tm_out, int* milliseconds_out) {
    time_t seconds = (time_t)(ms / 1000.0);
    *milliseconds_out = (int)((long long)ms % 1000);
    
    #ifdef _WIN32
        /* Windows: use localtime_s */
        localtime_s(tm_out, &seconds);
    #else
        /* POSIX: use localtime_r */
        localtime_r(&seconds, tm_out);
    #endif
}

/* Helper: Convert milliseconds to struct tm (UTC) */
static void ms_to_tm_utc(double ms, struct tm* tm_out, int* milliseconds_out) {
    time_t seconds = (time_t)(ms / 1000.0);
    *milliseconds_out = (int)((long long)ms % 1000);
    
    #ifdef _WIN32
        /* Windows: use gmtime_s */
        gmtime_s(tm_out, &seconds);
    #else
        /* POSIX: use gmtime_r */
        gmtime_r(&seconds, tm_out);
    #endif
}

/* Date:now(): num - Get current timestamp in milliseconds */
static as_value_t* std_date_now(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args;
    (void)arg_count;
    (void)userdata;
    
    return as_value_num(get_current_time_ms());
}

/* Date:year(date?): num - Get year */
static as_value_t* std_date_year(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    double ms = (arg_count >= 1 && args[0]->type == AS_VALUE_NUM) 
                ? args[0]->data.num_val 
                : get_current_time_ms();
    
    struct tm tm;
    int milliseconds;
    ms_to_tm(ms, &tm, &milliseconds);
    
    return as_value_num(1900 + tm.tm_year);
}

/* Date:month(date?): num - Get month (1-12) */
static as_value_t* std_date_month(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    double ms = (arg_count >= 1 && args[0]->type == AS_VALUE_NUM) 
                ? args[0]->data.num_val 
                : get_current_time_ms();
    
    struct tm tm;
    int milliseconds;
    ms_to_tm(ms, &tm, &milliseconds);
    
    return as_value_num(tm.tm_mon + 1);  /* C uses 0-11, AiScript uses 1-12 */
}

/* Date:day(date?): num - Get day of month (1-31) */
static as_value_t* std_date_day(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    double ms = (arg_count >= 1 && args[0]->type == AS_VALUE_NUM) 
                ? args[0]->data.num_val 
                : get_current_time_ms();
    
    struct tm tm;
    int milliseconds;
    ms_to_tm(ms, &tm, &milliseconds);
    
    return as_value_num(tm.tm_mday);
}

/* Date:hour(date?): num - Get hour (0-23) */
static as_value_t* std_date_hour(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    double ms = (arg_count >= 1 && args[0]->type == AS_VALUE_NUM) 
                ? args[0]->data.num_val 
                : get_current_time_ms();
    
    struct tm tm;
    int milliseconds;
    ms_to_tm(ms, &tm, &milliseconds);
    
    return as_value_num(tm.tm_hour);
}

/* Date:minute(date?): num - Get minute (0-59) */
static as_value_t* std_date_minute(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    double ms = (arg_count >= 1 && args[0]->type == AS_VALUE_NUM) 
                ? args[0]->data.num_val 
                : get_current_time_ms();
    
    struct tm tm;
    int milliseconds;
    ms_to_tm(ms, &tm, &milliseconds);
    
    return as_value_num(tm.tm_min);
}

/* Date:second(date?): num - Get second (0-59) */
static as_value_t* std_date_second(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    double ms = (arg_count >= 1 && args[0]->type == AS_VALUE_NUM) 
                ? args[0]->data.num_val 
                : get_current_time_ms();
    
    struct tm tm;
    int milliseconds;
    ms_to_tm(ms, &tm, &milliseconds);
    
    return as_value_num(tm.tm_sec);
}

/* Date:millisecond(date?): num - Get millisecond (0-999) */
static as_value_t* std_date_millisecond(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    double ms = (arg_count >= 1 && args[0]->type == AS_VALUE_NUM) 
                ? args[0]->data.num_val 
                : get_current_time_ms();
    
    int milliseconds = (int)((long long)ms % 1000);
    return as_value_num(milliseconds);
}

#ifndef NO_DATE_PARSE
static int64_t days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    {
        int era = (y >= 0 ? y : y - 399) / 400;
        unsigned yoe = (unsigned)(y - era * 400);
        unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
        unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return (int64_t)era * 146097 + (int64_t)doe - 719468;
    }
}

static int parse_rfc2822_month(const char* p)
{
    static const char* months = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int i;
    for (i = 0; i < 12; i++) {
        if (strncmp(p, months + i * 3, 3) == 0) {
            return i + 1;
        }
    }
    return 0;
}

/* Date:parse(date_str): num | error */
static as_value_t* std_date_parse(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    const char* s;
    int y = 0, mo = 1, d = 1, H = 0, M = 0, S = 0, ms = 0;
    int off_min = 0;
    int64_t days, unix_ms;
    
    if (arg_count < 1 || args[0]->type != AS_VALUE_STR) {
        return as_value_error("not_date", NULL);
    }
    s = args[0]->data.str_val;
    while (*s == ' ' || *s == '\t') {
        s++;
    }
    if (!*s) {
        return as_value_error("not_date", NULL);
    }

    /* RFC 2822: 01 Jan 1970 00:00:00 GMT */
    if (strlen(s) >= 20 && s[2] == ' ' && parse_rfc2822_month(s + 3)) {
        int day = 0, year = 0, hour = 0, min = 0, sec = 0;
        char mon[4] = {0};
        if (sscanf(s, "%d %3s %d %d:%d:%d", &day, mon, &year, &hour, &min, &sec) >= 3) {
            int month = parse_rfc2822_month(mon);
            if (month) {
                days = days_from_civil(year, month, day);
                unix_ms = days * 86400000LL + ((int64_t)hour * 3600 + min * 60 + sec) * 1000;
                return as_value_num((double)unix_ms);
            }
        }
    }

    /* ISO: YYYY-MM-DDTHH:MM:SS[.sss][Z|±HH:MM] or YYYY-MM-DD */
    {
        int y2, mo2, d2, H2 = 0, M2 = 0, S2 = 0;
        int oh = 0, om = 0;
        const char* p = s;
        int got = 0;
        if (sscanf(p, "%d-%d-%d", &y2, &mo2, &d2) != 3) {
            return as_value_error("not_date", NULL);
        }
        y = y2; mo = mo2; d = d2;
        p = strchr(s, 'T');
        if (!p) {
            p = strchr(s, 't');
        }
        if (p) {
            p++;
            got = sscanf(p, "%d:%d:%d", &H2, &M2, &S2);
            if (got < 2) {
                return as_value_error("not_date", NULL);
            }
            H = H2; M = M2; S = (got >= 3) ? S2 : 0;
            {
                const char* dot = strchr(p, '.');
                const char* z = strpbrk(p, "Z+-");
                int ms2 = 0;
                if (dot && (!z || dot < z)) {
                    int frac = 0, digits = 0;
                    const char* q = dot + 1;
                    while (*q >= '0' && *q <= '9' && digits < 3) {
                        frac = frac * 10 + (*q - '0');
                        digits++;
                        q++;
                    }
                    while (digits < 3) {
                        frac *= 10;
                        digits++;
                    }
                    ms2 = frac;
                }
                ms = ms2;
                z = strpbrk(p, "Z+-");
                if (z && *z == 'Z') {
                    off_min = 0;
                } else if (z && (*z == '+' || *z == '-')) {
                    if (sscanf(z + 1, "%d:%d", &oh, &om) >= 1) {
                        if (strchr(z + 1, ':') == NULL) {
                            om = 0;
                        }
                        off_min = oh * 60 + om;
                        if (*z == '-') {
                            off_min = -off_min;
                        }
                    }
                }
            }
        }
    }

    if (mo < 1 || mo > 12 || d < 1 || d > 31) {
        return as_value_error("not_date", NULL);
    }
    days = days_from_civil(y, mo, d);
    unix_ms = days * 86400000LL + ((int64_t)H * 3600 + M * 60 + S) * 1000 + ms;
    unix_ms -= (int64_t)off_min * 60000LL;
    return as_value_num((double)unix_ms);
}
#else
static as_value_t* std_date_parse(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args; (void)arg_count; (void)userdata;
    fprintf(stderr, "Date:parse is not available in this build (NO_DATE_PARSE)\n");
    return as_value_error("not_available", as_value_null());
}
#endif

#ifndef NO_DATE_TO_ISO_STR
/* Date:to_iso_str(date?, offset?): str - Convert to ISO 8601 string */
static as_value_t* std_date_to_iso_str(as_value_t** args, size_t arg_count, void* userdata) {
    (void)userdata;
    
    double ms = (arg_count >= 1 && args[0]->type == AS_VALUE_NUM) 
                ? args[0]->data.num_val 
                : get_current_time_ms();
    
    /* Get time offset in minutes (default: use local timezone) */
    bool use_utc = (arg_count < 2 || args[1]->type != AS_VALUE_NUM);
    int offset_minutes = use_utc ? 0 : (int)args[1]->data.num_val;
    
    struct tm tm;
    int milliseconds;
    
    if (use_utc) {
        ms_to_tm_utc(ms, &tm, &milliseconds);
    } else {
        /* Apply offset */
        ms += offset_minutes * 60 * 1000;
        ms_to_tm_utc(ms, &tm, &milliseconds);
    }
    
    /* Format: YYYY-MM-DDTHH:MM:SS.sssZ or YYYY-MM-DDTHH:MM:SS.sss+HH:MM */
    char buffer[64];
    
    if (use_utc || offset_minutes == 0) {
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec, milliseconds);
    } else {
        int offset_hours = offset_minutes / 60;
        int offset_mins = abs(offset_minutes % 60);
        char sign = offset_minutes >= 0 ? '+' : '-';
        
        snprintf(buffer, sizeof(buffer), "%04d-%02d-%02dT%02d:%02d:%02d.%03d%c%02d:%02d",
                tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                tm.tm_hour, tm.tm_min, tm.tm_sec, milliseconds,
                sign, abs(offset_hours), offset_mins);
    }
    
    return as_value_str(buffer);
}
#else
static as_value_t* std_date_to_iso_str(as_value_t** args, size_t arg_count, void* userdata) {
    (void)args; (void)arg_count; (void)userdata;
    fprintf(stderr, "Date:to_iso_str is not available in this build (NO_DATE_TO_ISO_STR)\n");
    return as_value_str("");
}
#endif

/* Register all Date functions */
void as_std_date_register(as_interpreter_t* interp) {
    as_scope_t* global = interp->global_scope;
    as_value_t* fn;
    
    fn = as_value_fn_native(std_date_now, NULL);
    as_scope_add(global, "Date:now", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_date_year, NULL);
    as_scope_add(global, "Date:year", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_date_month, NULL);
    as_scope_add(global, "Date:month", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_date_day, NULL);
    as_scope_add(global, "Date:day", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_date_hour, NULL);
    as_scope_add(global, "Date:hour", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_date_minute, NULL);
    as_scope_add(global, "Date:minute", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_date_second, NULL);
    as_scope_add(global, "Date:second", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_date_millisecond, NULL);
    as_scope_add(global, "Date:millisecond", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_date_parse, NULL);
    as_scope_add(global, "Date:parse", fn, false);
    as_value_release(fn);
    
    fn = as_value_fn_native(std_date_to_iso_str, NULL);
    as_scope_add(global, "Date:to_iso_str", fn, false);
    as_value_release(fn);
}

bool as_std_date_is_available(void) {
    return true;
}

#else /* NO_DATE */

/* Stub implementations when Date namespace is disabled */
void as_std_date_register(as_interpreter_t* interp) {
    (void)interp;
    fprintf(stderr, "Date namespace is not available in this build (NO_DATE)\n");
}

bool as_std_date_is_available(void) {
    return false;
}

#endif /* NO_DATE */

