/*
 * AiScript Standard Library - Date Namespace
 * 
 * Date/time functions using Unix timestamps (milliseconds).
 * 
 * Opt-out system:
 * - Date functions CAN be disabled via compiler flags:
 *   -DNO_DATE (disables entire namespace)
 *   -DNO_DATE_PARSE, -DNO_DATE_TO_ISO_STR (disable specific functions)
 *   -DDATE_USE_STARTUP_TIME (use startup time instead of RTC for embedded)
 */

#ifndef AISCRIPT_STD_DATE_H
#define AISCRIPT_STD_DATE_H

#include "interpreter.h"

/* Register all Date namespace functions with the interpreter */
void as_std_date_register(as_interpreter_t* interp);

/* Check if Date namespace is available */
bool as_std_date_is_available(void);

#endif /* AISCRIPT_STD_DATE_H */

