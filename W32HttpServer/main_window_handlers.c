#include "common.h"
#include "main_window_handlers.h"
#include "server.h"

LRESULT on_initialise(HWND window, HINSTANCE current_instance);
LRESULT on_start_click(HWND window, HWND button);

HWND g_server_log_window = NULL;
HWND g_request_log_window = NULL;
SOCKET g_server_socket = INVALID_SOCKET;

static void scroll_to_bottom(HWND window) {
	int min, max;
	GetScrollRange(window, SB_VERT, &min, &max);
	SetScrollPos(window, SB_VERT, max, FALSE);
	SendMessage(window, WM_VSCROLL, SB_BOTTOM, 0);
}

static void set_button_server_status(HWND button, BOOL started);

/* Window procedure */
LRESULT CALLBACK main_window_procedure(HWND window, UINT message, WPARAM param16, LPARAM param32)
{
	HWND start_button;
	switch(message){
	case W32HTTP_SERVERLOG:
		SendMessage(g_server_log_window, LB_INSERTSTRING, -1, param32);
		scroll_to_bottom(g_server_log_window);
		break;
	case W32HTTP_RESPONSELOG:
		SendMessage(g_request_log_window, LB_INSERTSTRING, -1, param32);
		scroll_to_bottom(g_request_log_window);
		break;
	case W32HTTP_SERVER_STARTED:
		SendMessage(g_server_log_window, LB_INSERTSTRING, -1, (LPARAM) "Server Started");
		scroll_to_bottom(g_request_log_window);
		break;
	case W32HTTP_SERVER_STOPPED:
		SendMessage(g_server_log_window, LB_INSERTSTRING, -1, (LPARAM) "Server Stopped");
		start_button = GetDlgItem(window, IDC_START_BUTTON);
		if (start_button != NULL) {
			set_button_server_status(start_button, FALSE);
		}
		break;
	case WM_COMMAND:
		switch(LOWORD(param16)) {
		case IDC_START_BUTTON:
			return on_start_click(window, (HWND) param32);
		default:
			break;
		};
		break;
	case WM_CREATE:
		return on_initialise(window, ((LPCREATESTRUCT) param32)->hInstance);
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(window, message, param16, param32);
	}
	return 0;
}


LRESULT on_initialise(HWND window, HINSTANCE current_instance) {
	RECT parentRect;
	int parentWidth;
	HWND ctrl;

#define margin 10

	GetClientRect(window, &parentRect);
	parentWidth = parentRect.right - parentRect.left;

	/* Build the whole UI */

	ctrl = CreateWindowEx(
		0,
		"STATIC", 
		"Server Log",
		WS_CHILD | WS_VISIBLE,
		margin,
		margin,
		parentWidth - 2 * margin,
		3 * margin,
		window,
		NULL,
		current_instance,
		NULL);
	SendMessage(ctrl, WM_SETFONT, (WPARAM) g_sans_font, 0);

	ctrl = CreateWindowEx(
		0, 
		"LISTBOX", "Server Log",
		WS_BORDER | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
		margin, 
		3 * margin, 
		parentWidth - 2 * margin , 10 * margin, window, NULL, current_instance, NULL);
	SendMessage(ctrl, WM_SETFONT, (WPARAM) g_mono_font, 0);

	g_server_log_window = ctrl;

	ctrl = CreateWindowEx(
		0,
		"STATIC", 
		"Request Log",
		WS_CHILD | WS_VISIBLE,
		margin,
		13 * margin,
		parentWidth - 2 * margin,
		3 * margin,
		window,
		NULL,
		current_instance,
		NULL);
	SendMessage(ctrl, WM_SETFONT, (WPARAM) g_sans_font, 0);

	ctrl = CreateWindowEx(
		0, 
		"LISTBOX", "Request Log",
		WS_BORDER | WS_CHILD | WS_VISIBLE | WS_VSCROLL,
		margin, 
		15 * margin, 
		parentWidth - 2 * margin , 
		12 * margin, 
		window, 
		NULL, 
		current_instance, 
		NULL);
	SendMessage(ctrl, WM_SETFONT, (WPARAM) g_mono_font, 0);

	g_request_log_window = ctrl;
	
	ctrl = CreateWindowEx(
		0,
		"STATIC", "Port",
		WS_CHILD | WS_VISIBLE,
		margin, 
		(55 * margin)/2, 
		10 * margin , 
		(5 * margin)/2, 
		window, 
		NULL, 
		current_instance, 
		NULL);
	SendMessage(ctrl, WM_SETFONT, (WPARAM) g_sans_font, 0);

	ctrl = CreateWindowEx(
		0, 
		"EDIT", "8080",
		WS_BORDER | WS_CHILD | WS_VISIBLE | ES_CENTER | ES_NUMBER ,
		5 * margin, 
		27 * margin, 
		10 * margin , 
		(5 * margin)/2, 
		window, 
		(HMENU) IDC_PORT_EDIT, 
		current_instance, 
		NULL);
	SendMessage(ctrl, WM_SETFONT, (WPARAM) g_sans_font, 0);

	ctrl = CreateWindowEx(
		0, 
		"BUTTON", "Start Listening",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
		20 * margin, 
		27 * margin, 
		12 * margin , 
		3 * margin, 
		window, 
		(HMENU) IDC_START_BUTTON, 
		current_instance, 
		NULL);
	SendMessage(ctrl, WM_SETFONT, (WPARAM) g_sans_font, 0);

	return 0;
}

void set_button_server_status(HWND button, BOOL started) {
	SetWindowLongPtr(button, GWLP_USERDATA, started);
	SetWindowText(button, started ? "Stop Listening": "Start Listening");
}

LRESULT on_start_click(HWND window, HWND button) {

	BOOL started = (BOOL) GetWindowLongPtr(button, GWLP_USERDATA);
	struct server_config_t cfg;

	int listen_port = 8080;
	char listen_str[6];
	HWND port_edit;

	port_edit = GetDlgItem(window, IDC_PORT_EDIT);
	GetWindowText(port_edit, listen_str, sizeof(listen_str));

	if (0 != strlen(listen_str)) {
		listen_port = atoi(listen_str);
	}
	cfg.listen_addr = "0.0.0.0"; // todo: configurable
	cfg.listen_port = listen_port;
	cfg.main_window = window;

	if (!started) {
		InterlockedIncrement(&g_is_server_run); // 0 -> 1
		SendMessage(port_edit, WM_ENABLE, FALSE, 0);
		start_server(&cfg);
	} else {
		if (ssocket != INVALID_SOCKET) {
			closesocket(ssocket); // force thread to quit accepting
		}
		InterlockedDecrement(&g_is_server_run); // 1 -> 0
		SendMessage(port_edit, WM_ENABLE, TRUE, 0);
	}
	started = !started;
	set_button_server_status(button, started);
	
	return 0;
}
