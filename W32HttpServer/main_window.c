#include "common.h"
#include "main_window_handlers.h"

#include <commctrl.h>

static LPSTR g_main_window_class_name = "W32HttpServerWindow";
static const int g_main_width = 800;
static const int g_main_height = 370;

HFONT g_sans_font = NULL, g_mono_font = NULL;
HWND g_main_window = NULL;

int WINAPI WinMain(HINSTANCE current_instance, 
				   HINSTANCE previous_instance, 
				   LPSTR cmd_line, 
				   int show_param)
{
	// These variables are for creating window
	WNDCLASSEX wcex;
	MSG msg;
	HICON app_icon;
	HCURSOR app_cursor;
	HBRUSH background_colour;
	BOOL is_wc_registered = FALSE;

	// These is to adjust the window UI
	int screen_width, screen_height;
	HMENU sys_menu;

	// The window title will be loaded from string table instead of being hard-coded
	char main_window_title[64] = {0};

	// Zeroing out structures, so we don't need to assign 0 or NULL
	ZeroMemory(&wcex, sizeof(WNDCLASSEX));
	ZeroMemory(&msg, sizeof(MSG));

	// Initialising common controls, this will apply themes if you're using Windows XP.
	InitCommonControls();

	// Loading resources
	app_icon			= LoadIcon(current_instance, MAKEINTRESOURCE(IDI_HTTP));
	app_cursor			= LoadCursor(current_instance, IDC_ARROW);
	background_colour	= GetSysColorBrush(COLOR_3DFACE); 

	// specifying window class structure
	wcex.cbSize			= sizeof(WNDCLASSEX);
	wcex.style			= CS_HREDRAW | CS_VREDRAW;
	wcex.hbrBackground	= background_colour; 
	wcex.hCursor		= app_cursor;
	wcex.hIcon			= app_icon;
	wcex.hIconSm		= app_icon;
	wcex.hInstance		= current_instance;
	wcex.lpfnWndProc	= main_window_procedure;
	wcex.lpszClassName  = g_main_window_class_name;

	is_wc_registered = RegisterClassEx(&wcex);

	if (!is_wc_registered) {
		MessageBox(NULL, "Error Registering Window Class", "Error", MB_ICONSTOP);
		return 0;
	}

	screen_width = GetSystemMetrics(SM_CXSCREEN);
	screen_height = GetSystemMetrics(SM_CYSCREEN);

	// Load Fonts
	g_sans_font = CreateFont(8, 0, 0, 0, FW_DONTCARE,
		FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS,
		"MS Sans Serif");

	if (!g_sans_font) {
		MessageBox(NULL, "Error loading font: sans", "Error", MB_ICONHAND);
		goto end;
	}

	g_mono_font = CreateFont(12, 0, 0, 0, FW_DONTCARE,
		FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS,
		"Courier");
	if (!g_mono_font) {
		MessageBox(NULL, "Error loading font: mono", "Error", MB_ICONHAND);
		goto end;
	}

	if (!LoadString(current_instance, IDS_MAIN_WINDOW_TITLE,
		main_window_title, sizeof(main_window_title) - 1)) {
		MessageBox(NULL, "Error loading main window title string!", "Error",
				MB_ICONHAND);
		goto end;
	}

	g_main_window = CreateWindowEx(
		WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
		g_main_window_class_name,
		main_window_title,
		(WS_DLGFRAME | WS_CAPTION | WS_POPUP | WS_SYSMENU),
		(screen_width - g_main_width)/2,
		(screen_height - g_main_height)/2,
		g_main_width,
		g_main_height,
		NULL,
		NULL,
		current_instance,
		NULL);

	if (!g_main_window) {
		MessageBox(NULL, "Error creating main window!", "Error", MB_ICONSTOP);
		goto end;
	}

	// Disable Restore, Size, and Maximize Button
	// 'EnableMenuItem' is a misnomer. Bad API design.
	sys_menu = GetSystemMenu(g_main_window, FALSE);
	EnableMenuItem(sys_menu, SC_MAXIMIZE, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(sys_menu, SC_RESTORE, MF_BYCOMMAND | MF_GRAYED);
	EnableMenuItem(sys_menu, SC_SIZE, MF_BYCOMMAND | MF_GRAYED);
	
	// Show and Paint Window
	ShowWindow(g_main_window, show_param);
	UpdateWindow(g_main_window);
	
	// Let's pump the messages for the main window
	while(GetMessage(&msg, g_main_window, 0, 0) > 0) {
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

end:
	// Unregister the main window class
	if (is_wc_registered) {
		UnregisterClass(g_main_window_class_name, current_instance);
	}

	return (int)msg.wParam;
}
