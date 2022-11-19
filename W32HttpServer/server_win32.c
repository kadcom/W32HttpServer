#include "common.h"
#include "server.h"
#include "logging.h"
#include "main_window_handlers.h"
#include "server_http.h"
#include <strsafe.h>

volatile LONG g_is_server_run = FALSE;
SOCKET ssocket = INVALID_SOCKET;

static DWORD WINAPI listener_thread_proc(LPVOID param);
static void run_server(struct server_config_t *cfg);
static int handle_post(SOCKET csocket, struct http_request_t *req);

char canned_success_response[] = "HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\n\r\nHello,World\r\n\r\n";
char canned_unsupported_response[] = "HTTP/1.0 501 Not Implemented\r\nContent-Type: text/plain\r\n\r\nUnsupported method\r\n\r\n";
char canned_error_response[] = "HTTP/1.0 405 Method Not Allowed\r\nContent-Type: text/plain\r\n\r\nCannot use this method, lah!\r\n\r\n";

int start_server(struct server_config_t *cfg) 
{
	HANDLE listener_thread;
	DWORD listener_thread_id;
	char startup_message[256] = {0};

	if (NULL == cfg) {
		return -1;
	}
	cfg->lock = CreateEvent(NULL, TRUE, FALSE, NULL);
	init_log(cfg->main_window);

	StringCchPrintf(startup_message, sizeof(startup_message) - 1, "Server running at port %d", cfg->listen_port);
	print_log(LOG_SERVER, startup_message);

	listener_thread = CreateThread(NULL, 0, listener_thread_proc, cfg, 0, &listener_thread_id);
	WaitForSingleObject(cfg->lock, INFINITE);
	return 0;
};

DWORD WINAPI listener_thread_proc(LPVOID param)
{
	
	HWND main_window;
	struct server_config_t *cfg = (struct server_config_t *)param;
	main_window = cfg->main_window;

	SendNotifyMessage(main_window, W32HTTP_SERVER_STARTED, 0, 0);
	run_server(cfg);
	SendNotifyMessage(main_window, W32HTTP_SERVER_STOPPED, 0, 0);
	return 0;
}

#if MODERN_SOCKET
static SOCKET make_socket(const char *listen_addr, u16 nport, struct addrinfo **pserv_info) {
	struct addrinfo hints;
	struct addrinfo *serv_info;
	char port[6] = {0};
	SOCKET ssocket;
	int yes = 1;

	ZeroMemory(&hints, sizeof(struct addrinfo));
	hints.ai_flags = AI_PASSIVE;
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;

	StringCchPrintf(port, sizeof(port)-1, "%d", nport);

	getaddrinfo(listen_addr, port, &hints, pserv_info);

	serv_info = *pserv_info;
	
	ssocket = socket(serv_info->ai_family, serv_info->ai_socktype, serv_info->ai_protocol);
	setsockopt(ssocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
	bind(ssocket, ((struct addrinfo *)serv_info)->ai_addr, 
		(int)((struct addrinfo *)serv_info)->ai_addrlen);

	return ssocket;
}
#else
static SOCKET make_socket(const char *listen_addr, u16 nport) {
	struct sockaddr_in server_info;
	//struct hostent *hp;
	//char *local_ip;
	SOCKET ssocket;
	int yes = 1;

	//hp = gethostbyname(listen_addr);

	//local_ip = inet_ntoa (*(struct in_addr *)hp->h_addr_list[0]);

	memset(&server_info, 0, sizeof(server_info));
	server_info.sin_family = AF_INET;
	server_info.sin_addr.s_addr = INADDR_ANY;
	server_info.sin_port = htons(nport);

	ssocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	setsockopt(ssocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
	
	bind(ssocket, (struct sockaddr *) &server_info, sizeof(struct sockaddr_in));

	return ssocket;
}

#endif

// the most important part
// TODO: make it portable
// TODO: make handlers
void run_server(struct server_config_t *cfg) {
	int backlog = 10;
	int addr_size;
	struct sockaddr_in client_addr;

#if MODERN_SOCKET
	void *serv_info;
#endif

	SOCKET csocket;
#define MTU 1024
	int recv_len;
	u8 recv_buf[MTU] = {0};
	u8 *buf, *cursor;
	u32 *end_message;
	struct http_request_t req;

	WSADATA wsa;
	WORD wsa_version = MAKEWORD(2,2);
	SYSTEM_INFO sys_info;
	WSAStartup(wsa_version, &wsa);


	GetSystemInfo(&sys_info);
	buf = VirtualAlloc(NULL, sys_info.dwPageSize, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

#if MODERN_SOCKET
	ssocket = make_socket(cfg->listen_addr, cfg->listen_port, (struct addrinfo **)&serv_info);
#else
	ssocket = make_socket(cfg->listen_addr, cfg->listen_port);
#endif
	SetEvent(cfg->lock);
	listen(ssocket, backlog);

	addr_size = sizeof(client_addr);

	while(g_is_server_run) {
		csocket = accept(ssocket, (struct sockaddr*) &client_addr, &addr_size);

		if (csocket == SOCKET_ERROR) {
			break;
		}

    log_printf(LOG_SERVER, "Client [0x%x] connected: %s:%d", (size_t)csocket,
        inet_ntoa(client_addr.sin_addr), client_addr.sin_port); 
		
    cursor = buf;

		for(;;) {
			recv_len = recv(csocket, (char*) recv_buf, MTU, 0);
			CopyMemory(buf, recv_buf, recv_len);
			cursor += recv_len;

			end_message = (u32 *)(cursor - 4);

			if (0x0A0D0A0D == *end_message) {
				break;
			}
		}
		*cursor = 0; // null end of buffer
		// dump request
		print_log(LOG_REQUEST, (char*) buf);
    parse_http_request(buf, (cursor - buf), &req);

		switch(req.method){
    case HTTP_POST:
      if ( handle_post(csocket, &req) < 0) {
        send(csocket, canned_unsupported_response, sizeof(canned_unsupported_response) - 1, 0);
      }
      break;
		case HTTP_GET:
			send(csocket, canned_success_response, sizeof(canned_success_response) - 1, 0);			
			break;
		default:
			send(csocket, canned_error_response, sizeof(canned_error_response) - 1, 0);
			break;
		}

    log_printf(LOG_SERVER, "Client [0x%x] closed: %s:%d", (size_t)csocket,
        inet_ntoa(client_addr.sin_addr), client_addr.sin_port); 
		
		closesocket(csocket);
	}

	VirtualFree(buf, 0, MEM_RELEASE);
	closesocket(ssocket);
	WSACleanup();
}

int handle_post(SOCKET csocket, struct http_request_t *req) {
  return -1;  
}
