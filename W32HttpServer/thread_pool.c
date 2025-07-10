#include "thread_pool.h"
#include "logging.h"

static DWORD WINAPI worker_thread_proc(LPVOID param);
static DWORD WINAPI iocp_worker_thread_proc(LPVOID param);

// Thread pool implementation for Windows 9x
int thread_pool_init(thread_pool_t *pool, u32 thread_count) {
    u32 i;
    
    if (NULL == pool || thread_count == 0 || thread_count > MAX_WORKER_THREADS) {
        return -1;
    }

    ZeroMemory(pool, sizeof(thread_pool_t));
    
    pool->thread_count = thread_count;
    pool->work_queue_head = NULL;
    pool->work_queue_tail = NULL;
    pool->free_work_count = MAX_PENDING_WORK;
    
    // Initialize free work items stack
    for (i = 0; i < MAX_PENDING_WORK; i++) {
        pool->free_work_items[i] = i;
    }
    
    // Create synchronization objects
    pool->work_semaphore = CreateSemaphore(NULL, 0, MAX_PENDING_WORK, NULL);
    pool->work_mutex = CreateMutex(NULL, FALSE, NULL);
    pool->shutdown_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    
    if (NULL == pool->work_semaphore || NULL == pool->work_mutex || NULL == pool->shutdown_event) {
        thread_pool_shutdown(pool);
        return -1;
    }
    
    // Create worker threads
    for (i = 0; i < thread_count; i++) {
        pool->worker_threads[i] = CreateThread(NULL, 0, worker_thread_proc, pool, 0, NULL);
        if (NULL == pool->worker_threads[i]) {
            thread_pool_shutdown(pool);
            return -1;
        }
    }
    
    log_printf(LOG_SERVER, "Thread pool initialized with %d worker threads", thread_count);
    return 0;
}

int thread_pool_queue_work(thread_pool_t *pool, void (*work_func)(void *), void *param) {
    work_item_t *item;
    u32 item_index;
    
    if (NULL == pool || NULL == work_func || pool->is_shutdown) {
        return -1;
    }
    
    // Get work item from free list
    WaitForSingleObject(pool->work_mutex, INFINITE);
    
    if (pool->free_work_count == 0) {
        ReleaseMutex(pool->work_mutex);
        return -1; // No free work items
    }
    
    item_index = pool->free_work_items[--pool->free_work_count];
    item = &pool->work_items[item_index];
    
    item->work_func = work_func;
    item->param = param;
    item->next = NULL;
    
    // Add to work queue
    if (NULL == pool->work_queue_head) {
        pool->work_queue_head = item;
        pool->work_queue_tail = item;
    } else {
        pool->work_queue_tail->next = item;
        pool->work_queue_tail = item;
    }
    
    ReleaseMutex(pool->work_mutex);
    
    // Signal worker thread
    ReleaseSemaphore(pool->work_semaphore, 1, NULL);
    
    return 0;
}

void thread_pool_shutdown(thread_pool_t *pool) {
    u32 i;
    
    if (NULL == pool) {
        return;
    }
    
    pool->is_shutdown = TRUE;
    
    if (pool->shutdown_event) {
        SetEvent(pool->shutdown_event);
    }
    
    // Wait for all worker threads to finish
    for (i = 0; i < pool->thread_count; i++) {
        if (pool->worker_threads[i]) {
            WaitForSingleObject(pool->worker_threads[i], 5000); // 5 second timeout
            CloseHandle(pool->worker_threads[i]);
        }
    }
    
    // Clean up synchronization objects
    if (pool->work_semaphore) {
        CloseHandle(pool->work_semaphore);
    }
    if (pool->work_mutex) {
        CloseHandle(pool->work_mutex);
    }
    if (pool->shutdown_event) {
        CloseHandle(pool->shutdown_event);
    }
    
    log_printf(LOG_SERVER, "Thread pool shutdown complete");
}

static DWORD WINAPI worker_thread_proc(LPVOID param) {
    thread_pool_t *pool = (thread_pool_t *)param;
    work_item_t *work_item;
    u32 item_index;
    HANDLE wait_handles[2];
    
    wait_handles[0] = pool->work_semaphore;
    wait_handles[1] = pool->shutdown_event;
    
    while (!pool->is_shutdown) {
        DWORD wait_result = WaitForMultipleObjects(2, wait_handles, FALSE, INFINITE);
        
        if (wait_result == WAIT_OBJECT_0 + 1) {
            // Shutdown event signaled
            break;
        } else if (wait_result == WAIT_OBJECT_0) {
            // Work available
            WaitForSingleObject(pool->work_mutex, INFINITE);
            
            if (pool->work_queue_head) {
                work_item = pool->work_queue_head;
                pool->work_queue_head = work_item->next;
                
                if (NULL == pool->work_queue_head) {
                    pool->work_queue_tail = NULL;
                }
                
                // Return work item to free list
                item_index = (u32)(work_item - pool->work_items);
                pool->free_work_items[pool->free_work_count++] = item_index;
                
                ReleaseMutex(pool->work_mutex);
                
                // Execute work function
                work_item->work_func(work_item->param);
            } else {
                ReleaseMutex(pool->work_mutex);
            }
        }
    }
    
    return 0;
}

// IOCP implementation for Windows NT 4.0+
int iocp_pool_init(iocp_pool_t *pool, u32 thread_count) {
    u32 i;
    SOCKET dummy_socket;
    DWORD bytes_returned;
    GUID accept_ex_guid;
    GUID get_accept_ex_sockaddrs_guid;
    
    if (NULL == pool || thread_count == 0 || thread_count > MAX_WORKER_THREADS) {
        return -1;
    }

    ZeroMemory(pool, sizeof(iocp_pool_t));
    
    pool->thread_count = thread_count;
    
    /* Initialize GUIDs for extension functions */
    accept_ex_guid = WSAID_ACCEPTEX;
    get_accept_ex_sockaddrs_guid = WSAID_GETACCEPTEXSOCKADDRS;
    
    // Create completion port
    pool->completion_port = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, thread_count);
    if (NULL == pool->completion_port) {
        return -1;
    }
    
    // Create shutdown event
    pool->shutdown_event = CreateEvent(NULL, TRUE, FALSE, NULL);
    if (NULL == pool->shutdown_event) {
        CloseHandle(pool->completion_port);
        return -1;
    }
    
    // Load extension functions
    dummy_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (dummy_socket == INVALID_SOCKET) {
        CloseHandle(pool->completion_port);
        CloseHandle(pool->shutdown_event);
        return -1;
    }
    
    if (WSAIoctl(dummy_socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &accept_ex_guid, sizeof(accept_ex_guid),
                 &pool->lpfnAcceptEx, sizeof(pool->lpfnAcceptEx),
                 &bytes_returned, NULL, NULL) == SOCKET_ERROR) {
        closesocket(dummy_socket);
        CloseHandle(pool->completion_port);
        CloseHandle(pool->shutdown_event);
        return -1;
    }
    
    if (WSAIoctl(dummy_socket, SIO_GET_EXTENSION_FUNCTION_POINTER,
                 &get_accept_ex_sockaddrs_guid, sizeof(get_accept_ex_sockaddrs_guid),
                 &pool->lpfnGetAcceptExSockAddrs, sizeof(pool->lpfnGetAcceptExSockAddrs),
                 &bytes_returned, NULL, NULL) == SOCKET_ERROR) {
        closesocket(dummy_socket);
        CloseHandle(pool->completion_port);
        CloseHandle(pool->shutdown_event);
        return -1;
    }
    
    closesocket(dummy_socket);
    
    // Create worker threads
    for (i = 0; i < thread_count; i++) {
        pool->worker_threads[i] = CreateThread(NULL, 0, iocp_worker_thread_proc, pool, 0, NULL);
        if (NULL == pool->worker_threads[i]) {
            iocp_pool_shutdown(pool);
            return -1;
        }
    }
    
    log_printf(LOG_SERVER, "IOCP thread pool initialized with %d worker threads", thread_count);
    return 0;
}

int iocp_pool_associate_socket(iocp_pool_t *pool, SOCKET socket, void *completion_key) {
    if (NULL == pool || INVALID_SOCKET == socket) {
        return -1;
    }
    
    if (NULL == CreateIoCompletionPort((HANDLE)socket, pool->completion_port, 
                                      (ULONG_PTR)completion_key, 0)) {
        return -1;
    }
    
    return 0;
}

void iocp_pool_shutdown(iocp_pool_t *pool) {
    u32 i;
    
    if (NULL == pool) {
        return;
    }
    
    pool->is_shutdown = TRUE;
    
    // Signal shutdown to all worker threads
    for (i = 0; i < pool->thread_count; i++) {
        PostQueuedCompletionStatus(pool->completion_port, 0, 0, NULL);
    }
    
    // Wait for all worker threads to finish
    for (i = 0; i < pool->thread_count; i++) {
        if (pool->worker_threads[i]) {
            WaitForSingleObject(pool->worker_threads[i], 5000); // 5 second timeout
            CloseHandle(pool->worker_threads[i]);
        }
    }
    
    // Clean up
    if (pool->completion_port) {
        CloseHandle(pool->completion_port);
    }
    if (pool->shutdown_event) {
        CloseHandle(pool->shutdown_event);
    }
    
    log_printf(LOG_SERVER, "IOCP thread pool shutdown complete");
}

static DWORD WINAPI iocp_worker_thread_proc(LPVOID param) {
    iocp_pool_t *pool = (iocp_pool_t *)param;
    DWORD bytes_transferred;
    ULONG_PTR completion_key;
    LPOVERLAPPED overlapped;
    client_context_t *ctx;
    
    while (!pool->is_shutdown) {
        BOOL result = GetQueuedCompletionStatus(pool->completion_port, 
                                              &bytes_transferred, 
                                              &completion_key, 
                                              &overlapped, 
                                              INFINITE);
        
        if (!result || (0 == bytes_transferred && 0 == completion_key && NULL == overlapped)) {
            // Shutdown signal or error
            break;
        }
        
        if (NULL == overlapped) {
            // Shutdown signal
            break;
        }
        
        // Get client context from overlapped structure
        ctx = (client_context_t *)((char *)overlapped - offsetof(client_context_t, overlapped));
        
        if (!result) {
            // I/O operation failed
            log_printf(LOG_SERVER, "I/O operation failed for client [0x%x], error: %d", 
                (size_t)ctx->client_socket, GetLastError());
            // Clean up client context
            if (ctx->client_socket != INVALID_SOCKET) {
                closesocket(ctx->client_socket);
                ctx->client_socket = INVALID_SOCKET;
            }
            free(ctx);
            continue;
        }
        
        // Process the completed I/O operation based on operation type
        switch (ctx->operation) {
            case OP_ACCEPT:
                handle_accept_completion(pool, ctx, bytes_transferred);
                break;
            case OP_RECV:
                handle_recv_completion(pool, ctx, bytes_transferred);
                break;
            case OP_SEND:
                handle_send_completion(pool, ctx, bytes_transferred);
                break;
            default:
                log_printf(LOG_SERVER, "Unknown I/O operation type: %d", ctx->operation);
                break;
        }
    }
    
    return 0;
}

// Async I/O functions for IOCP
int post_accept(iocp_pool_t *pool, SOCKET listen_socket, client_context_t *ctx) {
    DWORD bytes_received = 0;
    BOOL result;
    
    if (NULL == pool || INVALID_SOCKET == listen_socket || NULL == ctx) {
        return -1;
    }
    
    // Create accept socket
    ctx->client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (ctx->client_socket == INVALID_SOCKET) {
        return -1;
    }
    
    // Set up overlapped structure
    ZeroMemory(&ctx->overlapped, sizeof(OVERLAPPED));
    ctx->operation = OP_ACCEPT;
    
    // Post AcceptEx
    result = pool->lpfnAcceptEx(listen_socket, ctx->client_socket, 
                                ctx->recv_buffer, 0, 
                                sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
                                &bytes_received, &ctx->overlapped);
    
    if (!result && WSAGetLastError() != WSA_IO_PENDING) {
        closesocket(ctx->client_socket);
        ctx->client_socket = INVALID_SOCKET;
        return -1;
    }
    
    return 0;
}

int post_recv(iocp_pool_t *pool, client_context_t *ctx) {
    WSABUF wsabuf;
    DWORD bytes_received = 0;
    DWORD flags = 0;
    int result;
    
    if (NULL == pool || NULL == ctx || ctx->client_socket == INVALID_SOCKET) {
        return -1;
    }
    
    // Set up overlapped structure
    ZeroMemory(&ctx->overlapped, sizeof(OVERLAPPED));
    ctx->operation = OP_RECV;
    
    // Set up WSA buffer
    wsabuf.buf = ctx->recv_buffer;
    wsabuf.len = sizeof(ctx->recv_buffer) - 1;
    
    // Post WSARecv
    result = WSARecv(ctx->client_socket, &wsabuf, 1, &bytes_received, &flags, 
                     &ctx->overlapped, NULL);
    
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        return -1;
    }
    
    return 0;
}

int post_send(iocp_pool_t *pool, client_context_t *ctx, const char *data, u32 len) {
    WSABUF wsabuf;
    DWORD bytes_sent = 0;
    int result;
    
    if (NULL == pool || NULL == ctx || ctx->client_socket == INVALID_SOCKET || 
        NULL == data || len == 0 || len > sizeof(ctx->send_buffer)) {
        return -1;
    }
    
    // Copy data to send buffer
    memcpy(ctx->send_buffer, data, len);
    ctx->bytes_to_send = len;
    
    // Set up overlapped structure
    ZeroMemory(&ctx->overlapped, sizeof(OVERLAPPED));
    ctx->operation = OP_SEND;
    
    // Set up WSA buffer
    wsabuf.buf = ctx->send_buffer;
    wsabuf.len = len;
    
    // Post WSASend
    result = WSASend(ctx->client_socket, &wsabuf, 1, &bytes_sent, 0, 
                     &ctx->overlapped, NULL);
    
    if (result == SOCKET_ERROR && WSAGetLastError() != WSA_IO_PENDING) {
        return -1;
    }
    
    return 0;
}