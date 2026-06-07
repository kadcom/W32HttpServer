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
   single streamed chunk, so larger means fewer round-trips per response. */
#define HTTP_SEND_BUFFER_SIZE (8192)

/* What kind of response body, if any, the server is currently streaming to a
   client. Bodies are produced incrementally so that arbitrarily large files
   *and* directory listings flow through one small fixed buffer and are never
   held whole in memory (the project forbids dynamic allocation on the serving
   path). See stream_next_chunk() in server_win32.c. */
typedef enum {
    STREAM_NONE = 0,   /* idle, or the response was a self-contained inline buffer */
    STREAM_FILE,       /* body is an on-disk file, read block by block */
    STREAM_DIR         /* body is an HTML directory listing, generated on the fly */
} stream_kind_t;

/* Progress of the on-the-fly directory-listing generator (STREAM_DIR). The
   HTML page is emitted in three parts so that a listing of any length can be
   produced one buffer at a time. */
#define DIR_PHASE_INTRO   (0)   /* still owe the reader <html>..<h1>..<ul> + parent link */
#define DIR_PHASE_ENTRIES (1)   /* emitting one <li> per directory entry */
#define DIR_PHASE_FOOTER  (2)   /* still owe the closing </ul></body></html> */
#define DIR_PHASE_DONE    (3)   /* whole page emitted */

typedef struct client_context_t {
    SOCKET client_socket;
    SOCKADDR_IN client_addr;
    OVERLAPPED overlapped;
    char recv_buffer[4096];
    char send_buffer[HTTP_SEND_BUFFER_SIZE];
    u32 bytes_received;
    u32 bytes_to_send;
    io_operation_t operation;

    /* ---- Response-body streaming state ----
       Which generator (if any) feeds the body, plus the per-generator state.
       A body is sent in HTTP_SEND_BUFFER_SIZE chunks, so memory use is fixed
       regardless of how big the file or listing is. */
    stream_kind_t stream_kind;     /* STREAM_NONE / STREAM_FILE / STREAM_DIR */

    /* STREAM_FILE: read straight from disk. */
    HANDLE stream_file;            /* open file handle, or INVALID_HANDLE_VALUE */
    u32    stream_remaining;       /* bytes of the file body still to send */

    /* STREAM_DIR: walk the directory with FindFirstFile/FindNextFile and turn
       each entry into a list item as we go. */
    HANDLE          dir_find;          /* enumeration handle, or INVALID_HANDLE_VALUE */
    WIN32_FIND_DATA dir_pending;       /* one entry already fetched but not yet emitted */
    int             dir_has_pending;   /* non-zero when dir_pending holds a valid entry */
    int             dir_phase;         /* DIR_PHASE_* progress through the page */
    char            dir_url[MAX_PATH]; /* request path, used for the heading and links */

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
