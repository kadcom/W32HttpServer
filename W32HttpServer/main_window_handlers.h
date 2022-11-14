#ifndef W32HTTP_MAIN_WINDOW_HANDLERS_H
#define W32HTTP_MAIN_WINDOW_HANDLERS_H

#pragma once
#include "common.h"

/* Window Procedure a.k.a event handler */
LRESULT CALLBACK main_window_procedure(HWND window, UINT message, WPARAM param16, LPARAM param32);

/* Some global parameters */
extern HFONT g_sans_font, g_mono_font;

/* Reference to main window */
extern HWND g_main_window;

/* Reference to log window */
extern HWND g_server_log_window;
extern HWND g_request_log_window;

extern BOOL g_is_server_run;
extern SOCKET ssocket;

/* Window Message to send to main thread */

#define W32HTTP_SERVERLOG	(WM_USER + 1)
#define W32HTTP_RESPONSELOG (WM_USER + 2)

#define W32HTTP_SERVER_STARTED (WM_USER + 3)
#define W32HTTP_SERVER_STOPPED (WM_USER + 4)

#define IDC_START_BUTTON (304)
#define IDC_PORT_EDIT (305)

#endif /* W32HTTP_MAIN_WINDOW_HANDLERS_H */