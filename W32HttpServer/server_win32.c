#include "common.h"
#include "server.h"
#include "logging.h"
#include "main_window_handlers.h"
#include "server_http.h"
#include "platform_detect.h"
#include "thread_pool.h"
#include <strsafe.h>

volatile LONG g_is_server_run = FALSE;
SOCKET ssocket = INVALID_SOCKET;
static struct server_config_t *g_server_config = NULL;

static DWORD WINAPI listener_thread_proc(LPVOID param);
static void run_server_win9x(struct server_config_t *cfg);
static void run_server_nt(struct server_config_t *cfg);
static int handle_post(SOCKET csocket, struct http_request_t *req);
static void handle_client_win9x(void *param);
static client_context_t* create_client_context(SOCKET client_socket, SOCKADDR_IN *client_addr);
static void destroy_client_context(client_context_t *ctx);
static void process_client_request(client_context_t *ctx);

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
	
	// Set defaults if not specified
	if (cfg->max_clients == 0) {
		cfg->max_clients = 10;
	}
	if (cfg->worker_threads == 0) {
		cfg->worker_threads = 4;
	}
	
	// Detect platform
	if (detect_platform(&cfg->platform_info) != 0) {
		print_log(LOG_SERVER, "Failed to detect platform");
		return -1;
	}
	
	StringCchPrintf(startup_message, sizeof(startup_message) - 1, 
		"Detected platform: %s", cfg->platform_info.version_string);
	print_log(LOG_SERVER, startup_message);
	
	// Initialize threading system based on platform
	if (cfg->platform_info.type == PLATFORM_WINNT) {
		if (iocp_pool_init(&cfg->threading.iocp_pool, cfg->worker_threads) != 0) {
			print_log(LOG_SERVER, "Failed to initialize IOCP thread pool");
			return -1;
		}
		print_log(LOG_SERVER, "Using IOCP for NT platform");
	} else {
		if (thread_pool_init(&cfg->threading.thread_pool, cfg->worker_threads) != 0) {
			print_log(LOG_SERVER, "Failed to initialize thread pool");
			return -1;
		}
		print_log(LOG_SERVER, "Using thread pool for Win9x platform");
	}
	
	cfg->lock = CreateEvent(NULL, TRUE, FALSE, NULL);
	init_log(cfg->main_window);
	g_server_config = cfg;

	StringCchPrintf(startup_message, sizeof(startup_message) - 1, 
		"Server starting on port %d with %d worker threads", 
		cfg->listen_port, cfg->worker_threads);
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
	
	// Choose appropriate server implementation based on platform
	if (cfg->platform_info.type == PLATFORM_WINNT) {
		run_server_nt(cfg);
	} else {
		run_server_win9x(cfg);
	}
	
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

// Windows 9x server implementation using thread pool
static void run_server_win9x(struct server_config_t *cfg) {
	int backlog = cfg->max_clients;
	int addr_size;
	struct sockaddr_in client_addr;
	SOCKET csocket;
	client_context_t *client_ctx;

	WSADATA wsa;
	WORD wsa_version = MAKEWORD(2,2);
	WSAStartup(wsa_version, &wsa);

#if MODERN_SOCKET
	void *serv_info;
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
			inet_ntoa(client_addr.sin_addr), ntohs(client_addr.sin_port));

		// Create client context
		client_ctx = create_client_context(csocket, &client_addr);
		if (NULL == client_ctx) {
			log_printf(LOG_SERVER, "Failed to create client context");
			closesocket(csocket);
			continue;
		}

		// Queue work to thread pool
		if (thread_pool_queue_work(&cfg->threading.thread_pool, handle_client_win9x, client_ctx) != 0) {
			log_printf(LOG_SERVER, "Failed to queue client work");
			destroy_client_context(client_ctx);
			closesocket(csocket);
		}
	}

	closesocket(ssocket);
	WSACleanup();
}

// Windows NT server implementation using IOCP with overlapped I/O
static void run_server_nt(struct server_config_t *cfg) {
	int backlog;
	client_context_t *accept_ctx;
	u32 i;
	WSADATA wsa;
	WORD wsa_version;
#if MODERN_SOCKET
	void *serv_info;
#endif
	
	backlog = cfg->max_clients;
	wsa_version = MAKEWORD(2,2);
	WSAStartup(wsa_version, &wsa);

#if MODERN_SOCKET
	ssocket = make_socket(cfg->listen_addr, cfg->listen_port, (struct addrinfo **)&serv_info);
#else
	ssocket = make_socket(cfg->listen_addr, cfg->listen_port);
#endif
	
	// Associate listen socket with IOCP
	if (iocp_pool_associate_socket(&cfg->threading.iocp_pool, ssocket, NULL) != 0) {
		log_printf(LOG_SERVER, "Failed to associate listen socket with IOCP");
		closesocket(ssocket);
		WSACleanup();
		SetEvent(cfg->lock);
		return;
	}
	
	SetEvent(cfg->lock);
	listen(ssocket, backlog);

	// Post initial AcceptEx operations
	for (i = 0; i < cfg->worker_threads; i++) {
		accept_ctx = create_client_context(INVALID_SOCKET, NULL);
		if (accept_ctx && post_accept(&cfg->threading.iocp_pool, ssocket, accept_ctx) == 0) {
			log_printf(LOG_SERVER, "Posted AcceptEx #%d", i);
		} else {
			log_printf(LOG_SERVER, "Failed to post AcceptEx #%d", i);
			if (accept_ctx) {
				destroy_client_context(accept_ctx);
			}
		}
	}

	log_printf(LOG_SERVER, "NT server started with overlapped I/O");

	// Main server loop - just wait for shutdown
	while(g_is_server_run) {
		Sleep(1000); // Check every second
	}

	closesocket(ssocket);
	WSACleanup();
}

void stop_server(void) {
	InterlockedExchange(&g_is_server_run, FALSE);
	
	if (g_server_config) {
		if (g_server_config->platform_info.type == PLATFORM_WINNT) {
			iocp_pool_shutdown(&g_server_config->threading.iocp_pool);
		} else {
			thread_pool_shutdown(&g_server_config->threading.thread_pool);
		}
	}
	
	if (ssocket != INVALID_SOCKET) {
		closesocket(ssocket);
		ssocket = INVALID_SOCKET;
	}
	
	print_log(LOG_SERVER, "Server stopped");
}

static client_context_t* create_client_context(SOCKET client_socket, SOCKADDR_IN *client_addr) {
	client_context_t *ctx = (client_context_t *)malloc(sizeof(client_context_t));
	
	if (NULL == ctx) {
		return NULL;
	}
	
	ZeroMemory(ctx, sizeof(client_context_t));
	ctx->client_socket = client_socket;
	ctx->client_addr = *client_addr;
	ctx->bytes_received = 0;
	ctx->next = NULL;
	
	return ctx;
}

static void destroy_client_context(client_context_t *ctx) {
	if (ctx) {
		if (ctx->client_socket != INVALID_SOCKET) {
			closesocket(ctx->client_socket);
		}
		free(ctx);
	}
}

static void handle_client_win9x(void *param) {
	client_context_t *ctx = (client_context_t *)param;
	
	if (NULL == ctx) {
		return;
	}
	
	process_client_request(ctx);
	destroy_client_context(ctx);
}

static void process_client_request(client_context_t *ctx) {
	int recv_len;
	u8 *cursor;
	u32 *end_message;
	struct http_request_t req;
	
	cursor = (u8*)ctx->recv_buffer;
	
	// Receive HTTP request
	for(;;) {
		recv_len = recv(ctx->client_socket, (char*)cursor, 
			sizeof(ctx->recv_buffer) - (cursor - (u8*)ctx->recv_buffer) - 1, 0);
		
		if (recv_len <= 0) {
			break;
		}
		
		cursor += recv_len;
		ctx->bytes_received += recv_len;
		
		// Check for end of HTTP headers
		if (ctx->bytes_received >= 4) {
			end_message = (u32 *)(cursor - 4);
			if (0x0A0D0A0D == *end_message) {
				break;
			}
		}
		
		// Prevent buffer overflow
		if (ctx->bytes_received >= sizeof(ctx->recv_buffer) - 1) {
			break;
		}
	}
	
	*cursor = 0; // null terminate
	
	// Log the request
	log_printf(LOG_REQUEST, "Client [0x%x]: %s", (size_t)ctx->client_socket, ctx->recv_buffer);
	
	// Parse HTTP request
	parse_http_request((u8*)ctx->recv_buffer, ctx->bytes_received, &req);
	
	// Handle request based on method
	switch(req.method) {
		case HTTP_POST:
			if (handle_post(ctx->client_socket, &req) < 0) {
				send(ctx->client_socket, canned_unsupported_response, 
					sizeof(canned_unsupported_response) - 1, 0);
			}
			break;
		case HTTP_GET:
			send(ctx->client_socket, canned_success_response, 
				sizeof(canned_success_response) - 1, 0);
			break;
		default:
			send(ctx->client_socket, canned_error_response, 
				sizeof(canned_error_response) - 1, 0);
			break;
	}
	
	log_printf(LOG_SERVER, "Client [0x%x] processed: %s:%d", 
		(size_t)ctx->client_socket,
		inet_ntoa(ctx->client_addr.sin_addr), 
		ntohs(ctx->client_addr.sin_port));
}

int handle_post(SOCKET csocket, struct http_request_t *req) {
	return -1;  
}

// IOCP completion handlers
void handle_accept_completion(iocp_pool_t *pool, client_context_t *ctx, DWORD bytes_transferred) {
	SOCKADDR_IN *local_addr, *remote_addr;
	INT local_len, remote_len;
	client_context_t *new_accept_ctx;
	
	if (NULL == pool || NULL == ctx) {
		return;
	}
	
	// Get client address information
	pool->lpfnGetAcceptExSockAddrs(ctx->recv_buffer, 0,
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
		(SOCKADDR**)&local_addr, &local_len,
		(SOCKADDR**)&remote_addr, &remote_len);
	
	ctx->client_addr = *remote_addr;
	
	// Associate accepted socket with IOCP
	if (iocp_pool_associate_socket(pool, ctx->client_socket, ctx) != 0) {
		log_printf(LOG_SERVER, "Failed to associate accepted socket with IOCP");
		destroy_client_context(ctx);
		return;
	}
	
	log_printf(LOG_SERVER, "Client [0x%x] connected: %s:%d", (size_t)ctx->client_socket,
		inet_ntoa(ctx->client_addr.sin_addr), ntohs(ctx->client_addr.sin_port));
	
	// Post a receive operation for this client
	if (post_recv(pool, ctx) != 0) {
		log_printf(LOG_SERVER, "Failed to post receive for client [0x%x]", (size_t)ctx->client_socket);
		destroy_client_context(ctx);
		return;
	}
	
	// Post a new AcceptEx to keep accepting connections
	new_accept_ctx = create_client_context(INVALID_SOCKET, NULL);
	if (new_accept_ctx && post_accept(pool, ssocket, new_accept_ctx) != 0) {
		log_printf(LOG_SERVER, "Failed to post new AcceptEx");
		destroy_client_context(new_accept_ctx);
	}
}

void handle_recv_completion(iocp_pool_t *pool, client_context_t *ctx, DWORD bytes_transferred) {
	struct http_request_t req;
	const char *response;
	u32 response_len;
	
	if (NULL == pool || NULL == ctx) {
		return;
	}
	
	if (bytes_transferred == 0) {
		// Client disconnected
		log_printf(LOG_SERVER, "Client [0x%x] disconnected", (size_t)ctx->client_socket);
		destroy_client_context(ctx);
		return;
	}
	
	ctx->bytes_received = bytes_transferred;
	ctx->recv_buffer[bytes_transferred] = '\0';
	
	// Log the request
	log_printf(LOG_REQUEST, "Client [0x%x]: %s", (size_t)ctx->client_socket, ctx->recv_buffer);
	
	// Parse HTTP request
	parse_http_request((u8*)ctx->recv_buffer, ctx->bytes_received, &req);
	
	// Determine response based on method
	switch(req.method) {
		case HTTP_GET:
			response = canned_success_response;
			response_len = (u32)(sizeof(canned_success_response) - 1);
			break;
		case HTTP_POST:
			if (handle_post(ctx->client_socket, &req) < 0) {
				response = canned_unsupported_response;
				response_len = (u32)(sizeof(canned_unsupported_response) - 1);
			} else {
				response = canned_success_response;
				response_len = (u32)(sizeof(canned_success_response) - 1);
			}
			break;
		default:
			response = canned_error_response;
			response_len = (u32)(sizeof(canned_error_response) - 1);
			break;
	}
	
	// Send response
	if (post_send(pool, ctx, response, response_len) != 0) {
		log_printf(LOG_SERVER, "Failed to post send for client [0x%x]", (size_t)ctx->client_socket);
		destroy_client_context(ctx);
	}
}

void handle_send_completion(iocp_pool_t *pool, client_context_t *ctx, DWORD bytes_transferred) {
	if (NULL == pool || NULL == ctx) {
		return;
	}
	
	log_printf(LOG_SERVER, "Client [0x%x] response sent: %s:%d", 
		(size_t)ctx->client_socket,
		inet_ntoa(ctx->client_addr.sin_addr), 
		ntohs(ctx->client_addr.sin_port));
	
	// Close connection after sending response
	destroy_client_context(ctx);
}
