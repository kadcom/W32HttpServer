#ifndef W32HTTP_SERVER_H
#define W32HTTP_SERVER_H
#include "common.h"
#include "platform_detect.h"
#include "thread_pool.h"

typedef enum {
    OP_ACCEPT = 0,
    OP_RECV = 1,
    OP_SEND = 2
} io_operation_t;

/* Size of the per-client send buffer. This is also the maximum size of a
   single streamed file chunk, so larger means fewer round-trips per file. */
#define HTTP_SEND_BUFFER_SIZE (8192)

typedef struct client_context_t {
    SOCKET client_socket;
    SOCKADDR_IN client_addr;
    OVERLAPPED overlapped;
    char recv_buffer[4096];
    char send_buffer[HTTP_SEND_BUFFER_SIZE];
    u32 bytes_received;
    u32 bytes_to_send;
    io_operation_t operation;
    /* File-streaming state: a response body is sent in HTTP_SEND_BUFFER_SIZE
       chunks straight from disk, so files of any size use a fixed buffer. */
    HANDLE stream_file;        /* open file being streamed, or INVALID_HANDLE_VALUE */
    u32    stream_remaining;   /* bytes of the file body still to send */
    int    is_streaming;       /* non-zero while a file body is in flight */
    struct client_context_t *next;
} client_context_t;

struct server_config_t {
	char *listen_addr;
	u16   listen_port;
	u32   max_clients;
	u32   worker_threads;
	char document_root[MAX_PATH];

	HWND	main_window;
	HANDLE	lock;
	
	// Platform-specific threading
	platform_info_t platform_info;
	union {
		thread_pool_t thread_pool;  // For Win9x
		iocp_pool_t iocp_pool;      // For NT 4.0+
	} threading;
};

int start_server(struct server_config_t *cfg);
void stop_server(void);

#endif //W32HTTP_SERVER_H
