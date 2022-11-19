#include "common.h"
#include "logging.h"
#include "main_window_handlers.h"
#include <strsafe.h>

static HWND g_parent_log = NULL;

void init_log(HWND parent_window) {
	g_parent_log = parent_window;
}

void print_log(log_kind_t kind, char *str) {
	int msg;
	if (g_parent_log == NULL) {
		MessageBox(NULL, "you haven't attach a logging window", "warning", MB_ICONWARNING);
	}
	switch (kind){
	case LOG_REQUEST:
		msg = W32HTTP_RESPONSELOG;
		break;
	case LOG_SERVER:
	default:
		msg = W32HTTP_SERVERLOG;
	}
	SendNotifyMessage(g_parent_log, msg, 0, (LPARAM) str);
}

void log_printf(log_kind_t kind, char *fmt, ...) {
  char buf[1024] = {0};
  va_list args = NULL;
  va_start(args, fmt);

  StringCchVPrintf(buf, sizeof(buf) - 1, fmt, args);
  va_end(args);

  print_log(kind, buf);
}
