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

/* WM_DPICHANGED postdates the SDK this project targets (_WIN32_WINNT 0x400).
   We handle it for per-monitor DPI on modern Windows; on old Windows it simply
   never arrives. Declare it if the headers do not. */
#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
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

/* ===========================================================================
 *  DPI-aware, table-driven layout
 *
 *  The window is fixed-size, so "layout" here means: place a known set of
 *  controls crisply at any DPI and any UI font. Every control's geometry is
 *  given in integer LAYOUT UNITS (see g_layout[]); one unit is LAYOUT_UNIT_96
 *  pixels at 96 DPI and is scaled up on high-DPI displays. The whole UI keeps
 *  its proportions on a 96-DPI CRT and a 192-DPI laptop alike, with no
 *  hard-coded pixel positions. This is "dialog units, done by hand."
 * ========================================================================= */

#define LAYOUT_UNIT_96   5   /* one grid unit = 5px @ 96 DPI (the old margin/2) */
#define DESIGN_COLS    160   /* design client width, in units (= 800px @ 96)    */
#define PAD_UNITS        2   /* window edge padding, in units                   */

/* The "about" panel (a sunken box with an icon and a credit line) is a small
   composite, positioned as one block. */
#define ABOUT_X         80
#define ABOUT_Y         54
#define ABOUT_W         70
#define ABOUT_H         10

enum { FONT_SANS = 0, FONT_MONO = 1 };

#define CF_FULLWIDTH   0x01u  /* width spans the client minus side padding   */
#define CF_SERVERLOG   0x02u  /* remember this HWND in g_server_log_window   */
#define CF_REQUESTLOG  0x04u  /* remember this HWND in g_request_log_window  */

typedef struct {
	const char   *cls;        /* window class                                */
	const char   *text;       /* initial text                                */
	DWORD         style;      /* styles added to WS_CHILD | WS_VISIBLE        */
	int           id;         /* control id (0 = none)                       */
	short         x, y, w, h; /* geometry, in layout units                   */
	unsigned char font;       /* FONT_SANS / FONT_MONO                       */
	unsigned char flags;      /* CF_*                                        */
} ctl_def_t;

/* The whole UI in one readable table. Geometry is in layout units; the old
   hand-tuned pixel coordinates were all multiples of 5px, so they map 1:1. */
static const ctl_def_t g_layout[] = {
	/* class      text               style                              id                 x   y   w   h  font       flags */
	{ "STATIC",  "Server Log",       SS_LEFT,                           0,                 2,  2,  0,  6, FONT_SANS, CF_FULLWIDTH },
	{ "LISTBOX", "",                 WS_BORDER | WS_VSCROLL,            0,                 2,  6,  0, 20, FONT_MONO, CF_FULLWIDTH | CF_SERVERLOG },
	{ "STATIC",  "Request Log",      SS_LEFT,                           0,                 2, 26,  0,  6, FONT_SANS, CF_FULLWIDTH },
	{ "LISTBOX", "",                 WS_BORDER | WS_VSCROLL,            0,                 2, 30,  0, 24, FONT_MONO, CF_FULLWIDTH | CF_REQUESTLOG },
	{ "STATIC",  "Port",             SS_LEFT,                           0,                 2, 55, 20,  5, FONT_SANS, 0 },
	{ "EDIT",    "8080",             WS_BORDER | ES_CENTER | ES_NUMBER, IDC_PORT_EDIT,    10, 54, 20,  5, FONT_SANS, 0 },
	{ "STATIC",  "Folder to serve",  SS_LEFT,                           0,                 2, 58, 40,  5, FONT_SANS, 0 },
	{ "EDIT",    "",                 WS_BORDER | ES_LEFT | ES_READONLY, IDC_FOLDER_EDIT,   2, 62, 70,  5, FONT_SANS, 0 },
	{ "BUTTON",  "Browse...",        WS_TABSTOP | BS_PUSHBUTTON,        IDC_FOLDER_BUTTON,74, 62, 20,  5, FONT_SANS, 0 },
	{ "BUTTON",  "Start Listening",  WS_TABSTOP | BS_DEFPUSHBUTTON,     IDC_START_BUTTON, 40, 66, 24,  6, FONT_SANS, 0 }
};
#define LAYOUT_COUNT ((int)(sizeof(g_layout) / sizeof(g_layout[0])))

typedef struct {
	int dpi;       /* device DPI (96 = 100%)            */
	int unit;      /* one layout unit, in device pixels */
	int pad;       /* edge padding, in device pixels    */
	int client_w;  /* computed client width, in pixels  */
	int client_h;  /* computed client height, in pixels */
} layout_metrics_t;

/* Handles of the table controls, parallel to g_layout[], so we can reposition
   them on a DPI change. The about box is tracked separately. */
static HWND      g_ctl_hwnd[LAYOUT_COUNT];
static HWND      g_about_frame = NULL;
static HINSTANCE g_instance = NULL;

/* DPI of the monitor the window is on. Prefers the per-monitor GetDpiForWindow
   (Win10 1607+), resolved at run time, and falls back to the system DPI read
   from a DC, which works back to Win95. */
static int ui_get_dpi(HWND window) {
	typedef UINT (WINAPI *PFN_GetDpiForWindow)(HWND);
	static PFN_GetDpiForWindow pGetDpiForWindow = NULL;
	static int resolved = 0;
	int dpi = 96;

	if (!resolved) {
		HMODULE user32 = GetModuleHandle("user32.dll");
		pGetDpiForWindow = user32 ? (PFN_GetDpiForWindow) GetProcAddress(user32, "GetDpiForWindow") : NULL;
		resolved = 1;
	}
	if (pGetDpiForWindow != NULL) {
		UINT d = pGetDpiForWindow(window);
		if (d != 0) {
			return (int) d;
		}
	}
	{
		HDC dc = GetDC(window);
		if (dc != NULL) {
			int d = GetDeviceCaps(dc, LOGPIXELSX);
			ReleaseDC(window, dc);
			if (d != 0) {
				dpi = d;
			}
		}
	}
	return dpi;
}

/* (Re)create the UI fonts at a size appropriate for the given DPI. The faces
   and 96-DPI sizes match the originals, so the look is unchanged at 100% and
   simply scales up on high-DPI screens. Replaces g_sans_font / g_mono_font. */
static void set_ui_fonts(int dpi) {
	HFONT sans, mono;

	sans = CreateFont(MulDiv(8, dpi, 96), 0, 0, 0, FW_DONTCARE,
		FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS,
		"MS Sans Serif");
	mono = CreateFont(MulDiv(12, dpi, 96), 0, 0, 0, FW_DONTCARE,
		FALSE, FALSE, FALSE, ANSI_CHARSET, OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS,
		"Courier");

	if (sans != NULL) {
		if (g_sans_font != NULL) DeleteObject(g_sans_font);
		g_sans_font = sans;
	}
	if (mono != NULL) {
		if (g_mono_font != NULL) DeleteObject(g_mono_font);
		g_mono_font = mono;
	}
}

/* Fill in pixel metrics for the given DPI: the unit size, padding, and the
   client area needed to hold the laid-out content. */
static void compute_metrics(int dpi, layout_metrics_t *m) {
	int i;
	int bottom = ABOUT_Y + ABOUT_H;   /* the about box sits low; include it */

	m->dpi  = dpi;
	m->unit = MulDiv(LAYOUT_UNIT_96, dpi, 96);
	if (m->unit < 1) {
		m->unit = 1;
	}
	m->pad = PAD_UNITS * m->unit;

	for (i = 0; i < LAYOUT_COUNT; i++) {
		int b = g_layout[i].y + g_layout[i].h;
		if (b > bottom) {
			bottom = b;
		}
	}
	m->client_w = DESIGN_COLS * m->unit;
	m->client_h = (bottom + PAD_UNITS) * m->unit;
}

/* Create (create != 0) or reposition (create == 0) every table control for the
   given metrics. Geometry comes straight from g_layout[] scaled by m->unit. */
static void apply_layout(HWND parent, const layout_metrics_t *m, int create) {
	int i;
	for (i = 0; i < LAYOUT_COUNT; i++) {
		const ctl_def_t *c = &g_layout[i];
		HFONT font = (c->font == FONT_MONO) ? g_mono_font : g_sans_font;
		int x = c->x * m->unit;
		int y = c->y * m->unit;
		int w = (c->flags & CF_FULLWIDTH) ? (m->client_w - 2 * m->pad) : (c->w * m->unit);
		int h = c->h * m->unit;

		if (create) {
			HWND ctrl = CreateWindowEx(0, c->cls, c->text,
				WS_CHILD | WS_VISIBLE | c->style,
				x, y, w, h, parent,
				(HMENU)(LONG_PTR) c->id, g_instance, NULL);
			g_ctl_hwnd[i] = ctrl;
			if (ctrl != NULL) {
				SendMessage(ctrl, WM_SETFONT, (WPARAM) font, 0);
				if (c->flags & CF_SERVERLOG)  g_server_log_window  = ctrl;
				if (c->flags & CF_REQUESTLOG) g_request_log_window = ctrl;
			}
		} else if (g_ctl_hwnd[i] != NULL) {
			SetWindowPos(g_ctl_hwnd[i], NULL, x, y, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
			SendMessage(g_ctl_hwnd[i], WM_SETFONT, (WPARAM) font, (LPARAM) TRUE);
		}
	}
}

/* Build the sunken "about" panel: a frame holding the app icon and a credit
   line. Recreated wholesale on a DPI change so its contents rescale. */
static void create_about_box(HWND parent, const layout_metrics_t *m) {
	HICON icon = LoadIcon(g_instance, MAKEINTRESOURCE(IDI_HEAD));
	int inset = m->unit;                  /* small inner padding */
	int isz   = MulDiv(32, m->dpi, 96);   /* icon box, scaled    */
	HWND child;

	g_about_frame = CreateWindowEx(0, "STATIC", "",
		WS_CHILD | WS_VISIBLE | SS_SUNKEN,
		ABOUT_X * m->unit, ABOUT_Y * m->unit, ABOUT_W * m->unit, ABOUT_H * m->unit,
		parent, NULL, g_instance, NULL);

	child = CreateWindowEx(0, "STATIC", "",
		WS_CHILD | WS_VISIBLE | SS_ICON,
		inset, inset, isz, isz,
		g_about_frame, NULL, g_instance, NULL);
	SendMessage(child, STM_SETIMAGE, IMAGE_ICON, (LPARAM) icon);

	child = CreateWindowEx(0, "STATIC",
		"Simple HTTP Server by Didiet Noor\nhttp://www.retrocoding.net",
		WS_CHILD | WS_VISIBLE,
		inset * 2 + isz, inset,
		ABOUT_W * m->unit - (inset * 3 + isz), ABOUT_H * m->unit - inset * 2,
		g_about_frame, NULL, g_instance, NULL);
	SendMessage(child, WM_SETFONT, (WPARAM) g_sans_font, 0);
}

/* Resize and recentre the top-level window so its client area is exactly big
   enough for the laid-out content at the current DPI. Replaces the old
   hard-coded 800x370. */
static void size_window_to_content(HWND window, const layout_metrics_t *m) {
	RECT  r;
	DWORD style   = (DWORD) GetWindowLongPtr(window, GWL_STYLE);
	DWORD exstyle = (DWORD) GetWindowLongPtr(window, GWL_EXSTYLE);
	int   sw = GetSystemMetrics(SM_CXSCREEN);
	int   sh = GetSystemMetrics(SM_CYSCREEN);
	int   ww, wh;

	r.left = 0; r.top = 0; r.right = m->client_w; r.bottom = m->client_h;
	AdjustWindowRectEx(&r, style, FALSE, exstyle);
	ww = r.right - r.left;
	wh = r.bottom - r.top;
	MoveWindow(window, (sw - ww) / 2, (sh - wh) / 2, ww, wh, TRUE);
}

/* Re-run the whole layout for a new DPI: rescale fonts, reposition controls,
   and rebuild the about box. The caller repositions the top-level window. */
static void relayout_ui(HWND window, int dpi) {
	layout_metrics_t m;
	compute_metrics(dpi, &m);
	set_ui_fonts(dpi);
	apply_layout(window, &m, 0);
	if (g_about_frame != NULL) {
		DestroyWindow(g_about_frame);
		g_about_frame = NULL;
	}
	create_about_box(window, &m);
}

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
	case WM_DPICHANGED: {
		/* Per-monitor DPI changed (Win8.1+). wParam high word = new DPI;
		   lParam = the window rect Windows suggests for the new monitor. */
		RECT *suggested = (RECT *) param32;
		relayout_ui(window, (int) HIWORD(param16));
		SetWindowPos(window, NULL,
			suggested->left, suggested->top,
			suggested->right - suggested->left,
			suggested->bottom - suggested->top,
			SWP_NOZORDER | SWP_NOACTIVATE);
		return 0;
	}
	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(window, message, param16, param32);
	}
	return 0;
}

LRESULT on_initialise(HWND window, HINSTANCE current_instance) {
	layout_metrics_t m;

	g_instance = current_instance;

	/* Match the UI to the window's DPI before building anything. */
	compute_metrics(ui_get_dpi(window), &m);
	set_ui_fonts(m.dpi);

	/* Build the controls from the layout table, then the about panel. */
	apply_layout(window, &m, 1);
	create_about_box(window, &m);

	/* Finally, size the window to fit exactly what we laid out. */
	size_window_to_content(window, &m);

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
