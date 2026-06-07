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
static void handle_client_win9x(void *param);
static client_context_t* create_client_context(SOCKET client_socket, SOCKADDR_IN *client_addr);
static void destroy_client_context(client_context_t *ctx);
static void process_client_request(client_context_t *ctx);
static int serve_http_request(client_context_t *ctx, struct http_request_t *req, char* out, u32 out_size, u32* out_len);
static u32 stream_next_chunk(client_context_t *ctx, char* buf, u32 buf_size);
static int send_all(SOCKET sock, const char* buf, u32 len);

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
	if (cfg->document_root[0] == '\0') {
		/* No document root set - cannot start server */
		print_log(LOG_SERVER, "No document root specified. Please select a folder to serve.");
		return -1;
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
	ctx->stream_file = INVALID_HANDLE_VALUE;
	ctx->stream_remaining = 0;
	ctx->is_streaming = 0;
	ctx->next = NULL;

	return ctx;
}

static void destroy_client_context(client_context_t *ctx) {
	if (ctx) {
		/* Close any file still being streamed (e.g. client disconnected mid-send). */
		if (ctx->stream_file != INVALID_HANDLE_VALUE && ctx->stream_file != NULL) {
			CloseHandle(ctx->stream_file);
			ctx->stream_file = INVALID_HANDLE_VALUE;
		}
		ctx->is_streaming = 0;
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
		
		// Check for end of HTTP headers (CR LF CR LF). Compare bytes directly:
		// a (u32*) read here would be misaligned and faults on strict-alignment CPUs.
		if (ctx->bytes_received >= 4) {
			if (cursor[-4] == '\r' && cursor[-3] == '\n' &&
			    cursor[-2] == '\r' && cursor[-1] == '\n') {
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
	
	// Serve the request. serve_http_request fills `head` with either a complete
	// inline response or just the headers for a file to be streamed.
	{
		char head[HTTP_SEND_BUFFER_SIZE];
		char chunk[HTTP_SEND_BUFFER_SIZE];
		u32 head_len = 0;
		u32 n;
		int rc = serve_http_request(ctx, &req, head, sizeof(head), &head_len);

		if (rc < 0) {
			/* Catastrophic failure building a response. */
			build_http_error_response(500, "Internal Server Error",
				head, sizeof(head), &head_len);
			send_all(ctx->client_socket, head, head_len);
		} else {
			/* Send headers (or the whole inline response). */
			if (send_all(ctx->client_socket, head, head_len) == 0 && rc == 1) {
				/* Stream the file body in fixed-size chunks straight from disk. */
				while ((n = stream_next_chunk(ctx, chunk, sizeof(chunk))) > 0) {
					if (send_all(ctx->client_socket, chunk, n) != 0) {
						break;
					}
				}
			}
			if (ctx->stream_file != INVALID_HANDLE_VALUE) {
				CloseHandle(ctx->stream_file);
				ctx->stream_file = INVALID_HANDLE_VALUE;
			}
			ctx->is_streaming = 0;
		}
	}
	
	log_printf(LOG_SERVER, "Client [0x%x] processed: %s:%d", 
		(size_t)ctx->client_socket,
		inet_ntoa(ctx->client_addr.sin_addr), 
		ntohs(ctx->client_addr.sin_port));
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
	char head[HTTP_SEND_BUFFER_SIZE];
	u32 head_len = 0;
	int rc;

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

	// Serve the request. On rc == 1 a file was opened and ctx now streams its
	// body chunk-by-chunk across send completions (see handle_send_completion).
	rc = serve_http_request(ctx, &req, head, sizeof(head), &head_len);
	if (rc < 0) {
		build_http_error_response(500, "Internal Server Error",
			head, sizeof(head), &head_len);
		ctx->is_streaming = 0;
	}

	// Send headers (or the whole inline response). post_send copies `head` into
	// ctx->send_buffer, so subsequent chunks can safely reuse send_buffer.
	if (post_send(pool, ctx, head, head_len) != 0) {
		log_printf(LOG_SERVER, "Failed to post send for client [0x%x]", (size_t)ctx->client_socket);
		destroy_client_context(ctx);
	}
}

void handle_send_completion(iocp_pool_t *pool, client_context_t *ctx, DWORD bytes_transferred) {
	if (NULL == pool || NULL == ctx) {
		return;
	}

	// If a file body is in flight, post the next chunk and wait for its
	// completion rather than closing the connection now.
	if (ctx->is_streaming) {
		char chunk[HTTP_SEND_BUFFER_SIZE];
		u32 n = stream_next_chunk(ctx, chunk, sizeof(chunk));
		if (n > 0) {
			if (post_send(pool, ctx, chunk, n) == 0) {
				return; // more to send; resume on next send completion
			}
			// post_send failed: fall through to clean up and close.
		}
		// EOF (or send failure): close the file, then close the connection below.
		if (ctx->stream_file != INVALID_HANDLE_VALUE) {
			CloseHandle(ctx->stream_file);
			ctx->stream_file = INVALID_HANDLE_VALUE;
		}
		ctx->is_streaming = 0;
	}

	log_printf(LOG_SERVER, "Client [0x%x] response sent: %s:%d",
		(size_t)ctx->client_socket,
		inet_ntoa(ctx->client_addr.sin_addr),
		ntohs(ctx->client_addr.sin_port));

	// Close connection after sending response
	destroy_client_context(ctx);
}

/* Send `len` bytes over a blocking socket, looping until all are sent or the
   connection breaks. Plain send() may transmit fewer bytes than requested. */
static int send_all(SOCKET sock, const char* buf, u32 len) {
	u32 sent = 0;
	while (sent < len) {
		int n = send(sock, buf + sent, (int)(len - sent), 0);
		if (n == SOCKET_ERROR || n == 0) {
			return -1;
		}
		sent += (u32)n;
	}
	return 0;
}

/* Read the next block of the file being streamed into `buf`. Returns the number
   of bytes read (0 on EOF or error / when not streaming). */
static u32 stream_next_chunk(client_context_t *ctx, char* buf, u32 buf_size) {
	DWORD to_read;
	DWORD got = 0;

	if (!ctx->is_streaming || ctx->stream_file == INVALID_HANDLE_VALUE ||
	    ctx->stream_remaining == 0 || NULL == buf || buf_size == 0) {
		return 0;
	}

	to_read = buf_size;
	if (to_read > ctx->stream_remaining) {
		to_read = ctx->stream_remaining;
	}

	if (!ReadFile(ctx->stream_file, buf, to_read, &got, NULL) || got == 0) {
		return 0; /* read error or unexpected EOF */
	}

	ctx->stream_remaining -= got;
	return (u32)got;
}

/* Resolve a request to a response. On success either fills `out` with a
   complete inline response (return 0) or with just the HTTP headers and opens
   the file in ctx for chunked streaming (return 1). Returns -1 on failure. */
static int serve_http_request(client_context_t *ctx, struct http_request_t *req, char* out, u32 out_size, u32* out_len) {
	char full_path[MAX_PATH];
	char url_path[MAX_PATH];
	u8   peek[512];
	DWORD peeked = 0;
	DWORD file_size;
	DWORD file_attributes;
	const char* mime_type;
	HANDLE file_handle;
	int path_len;

	/* No file in flight yet for this request. */
	ctx->is_streaming = 0;
	ctx->stream_file = INVALID_HANDLE_VALUE;
	ctx->stream_remaining = 0;

	if (NULL == req || NULL == out || NULL == out_len || out_size == 0) {
		return -1;
	}

	*out_len = 0;

	/* Only handle GET requests. This is a read-only static file server, so any
	   other method (including POST) is correctly answered with 405. */
	if (req->method != HTTP_GET) {
		return build_http_error_response(405, "Method Not Allowed", out, out_size, out_len);
	}

	/* Extract URL path from request */
	path_len = (int)(req->path.end - req->path.start);
	if (path_len <= 0 || (u32)path_len >= sizeof(url_path)) {
		return build_http_error_response(400, "Bad Request", out, out_size, out_len);
	}

	/* Copy path and null-terminate */
	CopyMemory(url_path, req->path.start, path_len);
	url_path[path_len] = '\0';

	/* Resolve to full file system path */
	if (resolve_file_path(g_server_config->document_root, url_path, full_path, sizeof(full_path)) != 0) {
		return build_http_error_response(403, "Forbidden", out, out_size, out_len);
	}

	/* Check if path exists */
	file_attributes = GetFileAttributes(full_path);
	if (file_attributes == INVALID_FILE_ATTRIBUTES) {
		return build_http_error_response(404, "File Not Found", out, out_size, out_len);
	}

	/* Handle directory */
	if (file_attributes & FILE_ATTRIBUTE_DIRECTORY) {
		char index_path[MAX_PATH];
		char listing[HTTP_SEND_BUFFER_SIZE];
		u32  listing_len = 0;

		/* If the directory URL lacks a trailing slash, redirect so the browser
		   resolves relative links (and index.html assets) against the directory
		   rather than its parent. */
		if (url_path[path_len - 1] != '/') {
			char location[MAX_PATH + 2];
			wsprintf(location, "%s/", url_path);
			return build_http_redirect_response(location, out, out_size, out_len);
		}

		/* Prefer index.html if present - serve it as a streamed file. */
		wsprintf(index_path, "%s\\index.html", full_path);
		if (GetFileAttributes(index_path) != INVALID_FILE_ATTRIBUTES) {
			lstrcpy(full_path, index_path);
			/* fall through to the regular-file streaming path below */
		} else {
			/* Otherwise generate a directory listing as an inline response. */
			if (build_directory_listing(full_path, url_path, listing,
				sizeof(listing) - 512, &listing_len) != 0) {
				return build_http_error_response(500, "Cannot generate directory listing",
					out, out_size, out_len);
			}
			return build_http_file_response(NULL, "text/html", listing, listing_len,
				out, out_size, out_len);
		}
	}

	/* Regular file: open it and stream the body from disk in chunks. */
	file_handle = CreateFile(full_path, GENERIC_READ, FILE_SHARE_READ, NULL,
		OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	if (file_handle == INVALID_HANDLE_VALUE) {
		return build_http_error_response(500, "Cannot read file", out, out_size, out_len);
	}

	file_size = GetFileSize(file_handle, NULL);
	if (file_size == INVALID_FILE_SIZE) {
		CloseHandle(file_handle);
		return build_http_error_response(500, "Cannot read file", out, out_size, out_len);
	}

	/* Peek at the first bytes for content sniffing, then rewind so the stream
	   starts at the beginning. (detect_mime_type prefers the extension, so this
	   only matters for extension-less files.) */
	ReadFile(file_handle, peek, sizeof(peek), &peeked, NULL);
	SetFilePointer(file_handle, 0, NULL, FILE_BEGIN);
	mime_type = detect_mime_type(full_path, peek, (u32)peeked);

	/* Emit headers now; the body follows as streamed chunks. */
	if (build_http_file_header(mime_type, (u32)file_size, out, out_size, out_len) != 0) {
		CloseHandle(file_handle);
		return -1;
	}

	ctx->stream_file = file_handle;
	ctx->stream_remaining = (u32)file_size;
	ctx->is_streaming = 1;
	return 1; /* caller sends headers, then streams via stream_next_chunk */
}
