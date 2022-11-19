#ifndef W32HTTP_LOGGING_H
#define W32HTTP_LOGGING_H
#include "common.h"

typedef enum {
	LOG_SERVER,
	LOG_REQUEST,
} log_kind_t;

void init_log(HWND parent_window);
void print_log(log_kind_t kind, char *str);
void log_printf(log_kind_t kind, char *fmt, ...);

#endif //W32HTTP_LOGGING_H
