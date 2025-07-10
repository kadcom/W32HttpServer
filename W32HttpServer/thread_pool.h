#ifndef W32HTTP_THREAD_POOL_H
#define W32HTTP_THREAD_POOL_H

#include "common.h"

#define MAX_WORKER_THREADS 8
#define MAX_PENDING_WORK 64

typedef struct work_item_t {
    void (*work_func)(void *param);
    void *param;
    struct work_item_t *next;
} work_item_t;

typedef struct {
    HANDLE worker_threads[MAX_WORKER_THREADS];
    HANDLE work_semaphore;
    HANDLE work_mutex;
    HANDLE shutdown_event;
    
    work_item_t *work_queue_head;
    work_item_t *work_queue_tail;
    work_item_t work_items[MAX_PENDING_WORK];
    u32 free_work_items[MAX_PENDING_WORK];
    u32 free_work_count;
    
    u32 thread_count;
    BOOL is_shutdown;
} thread_pool_t;

typedef struct {
    HANDLE completion_port;
    HANDLE worker_threads[MAX_WORKER_THREADS];
    HANDLE shutdown_event;
    u32 thread_count;
    BOOL is_shutdown;
    
    // Extension function pointers
    LPFN_ACCEPTEX lpfnAcceptEx;
    LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockAddrs;
} iocp_pool_t;

// Thread pool functions (for Win9x)
int thread_pool_init(thread_pool_t *pool, u32 thread_count);
int thread_pool_queue_work(thread_pool_t *pool, void (*work_func)(void *), void *param);
void thread_pool_shutdown(thread_pool_t *pool);

// IOCP functions (for NT 4.0+)
int iocp_pool_init(iocp_pool_t *pool, u32 thread_count);
int iocp_pool_associate_socket(iocp_pool_t *pool, SOCKET socket, void *completion_key);
void iocp_pool_shutdown(iocp_pool_t *pool);

// Forward declarations for server functions
struct client_context_t;
void handle_accept_completion(iocp_pool_t *pool, struct client_context_t *ctx, DWORD bytes_transferred);
void handle_recv_completion(iocp_pool_t *pool, struct client_context_t *ctx, DWORD bytes_transferred);
void handle_send_completion(iocp_pool_t *pool, struct client_context_t *ctx, DWORD bytes_transferred);
int post_accept(iocp_pool_t *pool, SOCKET listen_socket, struct client_context_t *ctx);
int post_recv(iocp_pool_t *pool, struct client_context_t *ctx);
int post_send(iocp_pool_t *pool, struct client_context_t *ctx, const char *data, u32 len);

#endif // W32HTTP_THREAD_POOL_H