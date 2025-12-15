#ifndef LOG_H
#define LOG_H

#include <stdarg.h>

#include "clowncommon/clowncommon.h"

#ifdef __cplusplus
extern "C" {
#endif

void SetLogCallback(void (*log_callback)(void *user_data, const char *format, va_list arg), const void *user_data);
CC_ATTRIBUTE_PRINTF(1, 2) void LogMessage(const char *format, ...);
void LogMessageV(const char* const format, va_list args);

#ifdef __cplusplus
}
#endif

#endif /* ERROR_H */
