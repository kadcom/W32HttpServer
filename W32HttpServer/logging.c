#include "common.h"
#include "logging.h"
#include "main_window_handlers.h"

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