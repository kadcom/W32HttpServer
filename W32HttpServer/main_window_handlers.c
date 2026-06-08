#include "common.h"
#include "main_window_handlers.h"
#include "server.h"
#include <shlobj.h>

/* Old SDK headers (Watcom, VC6 without updated Platform SDK) predate the
   "new style" folder browser and do not define this flag. Define it ourselves
   so the code still builds there; at run time we only actually request it when
   COM is available (see on_folder_select_click). */
#ifndef BIF_NEWDIALOGSTYLE
#define BIF_NEWDIALOGSTYLE 0x0040
#endif

/* ---------------------------------------------------------------------------
 * Runtime resolution of the shell / OLE entry points.
 *
 * Rather than link shell32.dll and ole32.dll (which would make the loader
 * refuse to start the program if either were missing an expected export), we
 * load them with LoadLibrary and look the functions up with GetProcAddress.
 * The folder picker then works only if the lookups succeed, and the rest of
 * the program is unaffected. This mirrors how the IOCP path already resolves
 * AcceptEx at run time, and keeps the link line free of extra dependencies.
 * ------------------------------------------------------------------------- */
typedef HRESULT      (WINAPI *PFN_OleInitialize)(LPVOID);
typedef void         (WINAPI *PFN_OleUninitialize)(void);
typedef LPITEMIDLIST (WINAPI *PFN_SHBrowseForFolder)(LPBROWSEINFO);
typedef BOOL         (WINAPI *PFN_SHGetPathFromIDList)(LPCITEMIDLIST, LPSTR);
typedef HRESULT      (WINAPI *PFN_SHGetMalloc)(LPMALLOC *);

static HMODULE g_ole32_module   = NULL;
static HMODULE g_shell32_module = NULL;

static PFN_OleInitialize       pfn_OleInitialize       = NULL;
static PFN_OleUninitialize     pfn_OleUninitialize     = NULL;
static PFN_SHBrowseForFolder   pfn_SHBrowseForFolder   = NULL;
static PFN_SHGetPathFromIDList pfn_SHGetPathFromIDList = NULL;
static PFN_SHGetMalloc         pfn_SHGetMalloc         = NULL;

/* Non-zero once OleInitialize() has actually succeeded; gates the use of the
   "new style" browser, which needs COM running. */
static BOOL g_ole_available = FALSE;

void folder_picker_init(void) {
	/* ole32: COM init for the new-style dialog (optional enhancement). */
	g_ole32_module = LoadLibrary("ole32.dll");
	if (g_ole32_module != NULL) {
		pfn_OleInitialize   = (PFN_OleInitialize)   GetProcAddress(g_ole32_module, "OleInitialize");
		pfn_OleUninitialize = (PFN_OleUninitialize) GetProcAddress(g_ole32_module, "OleUninitialize");
	}

	/* shell32: the folder browser itself. Try the explicit ANSI export names,
	   falling back to the undecorated names some early Win9x shells used. */
	g_shell32_module = LoadLibrary("shell32.dll");
	if (g_shell32_module != NULL) {
		pfn_SHBrowseForFolder = (PFN_SHBrowseForFolder) GetProcAddress(g_shell32_module, "SHBrowseForFolderA");
		if (pfn_SHBrowseForFolder == NULL) {
			pfn_SHBrowseForFolder = (PFN_SHBrowseForFolder) GetProcAddress(g_shell32_module, "SHBrowseForFolder");
		}
		pfn_SHGetPathFromIDList = (PFN_SHGetPathFromIDList) GetProcAddress(g_shell32_module, "SHGetPathFromIDListA");
		if (pfn_SHGetPathFromIDList == NULL) {
			pfn_SHGetPathFromIDList = (PFN_SHGetPathFromIDList) GetProcAddress(g_shell32_module, "SHGetPathFromIDList");
		}
		pfn_SHGetMalloc = (PFN_SHGetMalloc) GetProcAddress(g_shell32_module, "SHGetMalloc");
	}

	/* Bring COM up only if we found OleInitialize. Remember success so we can
	   request the new dialog style and balance with OleUninitialize at exit. */
	if (pfn_OleInitialize != NULL && SUCCEEDED(pfn_OleInitialize(NULL))) {
		g_ole_available = TRUE;
	}
}

void folder_picker_shutdown(void) {
	if (g_ole_available && pfn_OleUninitialize != NULL) {
		pfn_OleUninitialize();
		g_ole_available = FALSE;
	}
	if (g_shell32_module != NULL) {
		FreeLibrary(g_shell32_module);
		g_shell32_module = NULL;
	}
	if (g_ole32_module != NULL) {
		FreeLibrary(g_ole32_module);
		g_ole32_module = NULL;
	}
}

LRESULT on_initialise(HWND window, HINSTANCE current_instance);
LRESULT on_start_click(HWND window, HWND button);
LRESULT on_folder_select_click(HWND window, HWND button);

HWND g_server_log_window = NULL;
HWND g_request_log_window = NULL;
SOCKET g_server_socket = INVALID_SOCKET;
static char g_document_root[MAX_PATH] = {0};

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
		case IDC_FOLDER_BUTTON:
			return on_folder_select_click(window, (HWND) param32);
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
	HWND frame;
	HICON head_icon;

#define margin 10

	GetClientRect(window, &parentRect);
	parentWidth = parentRect.right - parentRect.left;

	head_icon = LoadIcon(current_instance, MAKEINTRESOURCE(IDI_HEAD));

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

	/* Folder selection controls */
	ctrl = CreateWindowEx(
		0,
		"STATIC", "Folder to serve",
		WS_CHILD | WS_VISIBLE,
		margin, 
		29 * margin, 
		20 * margin , 
		(5 * margin)/2, 
		window, 
		NULL, 
		current_instance, 
		NULL);
	SendMessage(ctrl, WM_SETFONT, (WPARAM) g_sans_font, 0);

	ctrl = CreateWindowEx(
		0, 
		"EDIT", "",
		WS_BORDER | WS_CHILD | WS_VISIBLE | ES_LEFT | ES_READONLY,
		margin, 
		31 * margin, 
		35 * margin , 
		(5 * margin)/2, 
		window, 
		(HMENU) IDC_FOLDER_EDIT, 
		current_instance, 
		NULL);
	SendMessage(ctrl, WM_SETFONT, (WPARAM) g_sans_font, 0);

	ctrl = CreateWindowEx(
		0, 
		"BUTTON", "Browse...",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
		37 * margin, 
		31 * margin, 
		10 * margin , 
		(5 * margin)/2, 
		window, 
		(HMENU) IDC_FOLDER_BUTTON, 
		current_instance, 
		NULL);
	SendMessage(ctrl, WM_SETFONT, (WPARAM) g_sans_font, 0);

	ctrl = CreateWindowEx(
		0, 
		"BUTTON", "Start Listening",
		WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
		20 * margin, 
		33 * margin, 
		12 * margin , 
		3 * margin, 
		window, 
		(HMENU) IDC_START_BUTTON, 
		current_instance, 
		NULL);
	SendMessage(ctrl, WM_SETFONT, (WPARAM) g_sans_font, 0);

	ctrl = CreateWindowEx(
		0,
		"STATIC", "",
		WS_CHILD | WS_VISIBLE | SS_SUNKEN,
		40 * margin,
		27 * margin,
		350,
		48,
		window,
		NULL,
		current_instance,
		NULL);

	frame = ctrl;
	
	ctrl = CreateWindowEx(
		0,
		"STATIC", "",
		WS_CHILD | WS_VISIBLE | SS_ICON,
		8,
		8,
		32,
		32,
		frame,
		NULL,
		current_instance,
		NULL);
	SendMessage(ctrl, STM_SETIMAGE, IMAGE_ICON, (LPARAM) head_icon);

	ctrl = CreateWindowEx(
		0,
		"STATIC", "Simple HTTP Server by Didiet Noor\nhttp://www.retrocoding.net",
		WS_CHILD | WS_VISIBLE,
		52,
		8,
		250,
		32,
		frame,
		NULL,
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
	cfg.max_clients = 10;     // default max clients
	cfg.worker_threads = 4;   // default worker threads
	lstrcpy(cfg.document_root, g_document_root);

	if (!started) {
		/* Check if folder is selected */
		if (g_document_root[0] == '\0') {
			MessageBox(window, "Please select a folder to serve first.", "No Folder Selected", MB_ICONWARNING | MB_OK);
			return 0;
		}
		
		InterlockedIncrement(&g_is_server_run); // 0 -> 1
		SendMessage(port_edit, WM_ENABLE, FALSE, 0);
		SendMessage(GetDlgItem(window, IDC_FOLDER_BUTTON), WM_ENABLE, FALSE, 0);
		start_server(&cfg);
	} else {
		stop_server();
		SendMessage(port_edit, WM_ENABLE, TRUE, 0);
		SendMessage(GetDlgItem(window, IDC_FOLDER_BUTTON), WM_ENABLE, TRUE, 0);
	}
	started = !started;
	set_button_server_status(button, started);
	
	return 0;
}

LRESULT on_folder_select_click(HWND window, HWND button) {
	BROWSEINFO browse_info;
	LPITEMIDLIST item_id_list;
	char selected_path[MAX_PATH];
	HWND folder_edit;
	
	/* The browser and the path-extraction call are essential; if either could
	   not be resolved at startup there is no way to pick a folder, so say so
	   instead of crashing on a NULL function pointer. */
	if (pfn_SHBrowseForFolder == NULL || pfn_SHGetPathFromIDList == NULL) {
		MessageBox(window,
			"Folder browsing is not available on this system (shell32 could not "
			"be loaded).", "Folder Picker Unavailable", MB_ICONWARNING | MB_OK);
		return 0;
	}

	ZeroMemory(&browse_info, sizeof(BROWSEINFO));
	ZeroMemory(selected_path, sizeof(selected_path));

	browse_info.hwndOwner = window;
	browse_info.pszDisplayName = selected_path;
	browse_info.lpszTitle = "Select folder to serve:";
	/* Always restrict the result to real file-system directories. Only ask for
	   the modern dialog when COM is up; on an old Win9x shell without it we use
	   the classic browser, which is always available. */
	browse_info.ulFlags = BIF_RETURNONLYFSDIRS;
	if (g_ole_available) {
		browse_info.ulFlags |= BIF_NEWDIALOGSTYLE;
	}

	item_id_list = pfn_SHBrowseForFolder(&browse_info);

	if (item_id_list != NULL) {
		if (pfn_SHGetPathFromIDList(item_id_list, selected_path)) {
			/* Update the folder edit control */
			folder_edit = GetDlgItem(window, IDC_FOLDER_EDIT);
			if (folder_edit != NULL) {
				SetWindowText(folder_edit, selected_path);
				/* Store in global variable */
				lstrcpy(g_document_root, selected_path);
			}
		}

		/* Free the PIDL the shell allocated for us. If SHGetMalloc could not be
		   resolved we simply skip the free (a tiny one-time leak) rather than
		   risk calling through a NULL pointer. */
		if (pfn_SHGetMalloc != NULL) {
			LPMALLOC malloc_interface;
			if (SUCCEEDED(pfn_SHGetMalloc(&malloc_interface))) {
				malloc_interface->lpVtbl->Free(malloc_interface, item_id_list);
				malloc_interface->lpVtbl->Release(malloc_interface);
			}
		}
	}

	return 0;
}
