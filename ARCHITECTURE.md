# W32HttpServer Architecture Documentation

## Overview

W32HttpServer is a lightweight HTTP server written in pure C using the Windows API, designed to run on any Windows system from Windows 95 OSR2 to Windows 11. The project demonstrates how to build efficient network servers using platform-specific optimizations while maintaining backward compatibility.

## Design Philosophy

### Core Principles
- **Backward Compatibility**: Must run on Windows 95 OSR2 and Windows NT 4.0
- **Platform Optimization**: Use the best available APIs for each Windows platform
- **Minimal Dependencies**: Pure Windows API with no external libraries
- **GUI Integration**: Traditional Windows application with full graphical interface
- **Conservative Resource Usage**: Designed for systems with limited memory and CPU

### Architecture Goals
- Single binary compatibility across 25+ years of Windows versions
- Efficient multi-client handling using platform-appropriate techniques
- Thread-safe logging and GUI communication
- Graceful resource management and cleanup

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Main Window (GUI)                        │
│  ┌─────────────────┐  ┌─────────────────┐  ┌──────────────┐ │
│  │  Server Log     │  │  Request Log    │  │  Controls    │ │
│  │  (ListBox)      │  │  (ListBox)      │  │ Start/Stop   │ │
│  └─────────────────┘  └─────────────────┘  │ Port Config  │ │
│                                            └──────────────┘ │
└─────────────────────────────────────────────────────────────┘
                             │
                    Windows Messages
                             │
┌─────────────────────────────────────────────────────────────┐
│                    Server Core                              │
│                                                             │
│  ┌─────────────────┐                 ┌─────────────────────┐│
│  │ Platform        │    ┌─────────┐  │ Threading System    ││
│  │ Detection       │───▶│ Config  │  │                     ││
│  │ Win9x vs NT     │    └─────────┘  │ ┌─────────────────┐ ││
│  └─────────────────┘                 │ │ Win9x: Thread   │ ││
│                                      │ │ Pool + Semaphore│ ││
│  ┌─────────────────┐                 │ └─────────────────┘ ││
│  │ HTTP Parser     │                 │ ┌─────────────────┐ ││
│  │ Request/Response│                 │ │ NT: IOCP +      │ ││
│  │ Processing      │                 │ │ Overlapped I/O  │ ││
│  └─────────────────┘                 │ └─────────────────┘ ││
│                                      └─────────────────────┘│
└─────────────────────────────────────────────────────────────┘
                             │
                    Winsock2 API
                             │
┌─────────────────────────────────────────────────────────────┐
│                    Network Layer                            │
└─────────────────────────────────────────────────────────────┘
```

## Component Architecture

### 1. Main Window (`main_window.c`)

**Purpose**: Windows GUI application entry point and user interface management.

**Key Responsibilities**:
- Window creation and management using pure Win32 API
- Control layout and font management
- Event handling and user input processing
- Resource loading (icons, strings)

**Design Patterns**:
- Traditional Win32 message-driven architecture
- Manual control positioning (no UI framework dependencies)
- Resource management through Windows resource files

### 2. Server Core (`server_win32.c`)

**Purpose**: Core networking logic with platform-specific optimizations.

**Key Components**:

#### Platform Detection
```c
typedef enum {
    PLATFORM_WIN9X = 0,  // Windows 95/98/ME
    PLATFORM_WINNT = 1,  // Windows NT 4.0+
    PLATFORM_UNKNOWN = 2
} platform_type_t;
```

#### Socket Management
- **Win9x**: Traditional `accept()` with thread pool
- **NT**: `AcceptEx()` with IOCP for scalable async I/O

#### Connection Handling
```c
typedef struct client_context_t {
    SOCKET client_socket;
    SOCKADDR_IN client_addr;
    OVERLAPPED overlapped;     // For NT async I/O
    char recv_buffer[4096];
    char send_buffer[4096];
    u32 bytes_received;
    u32 bytes_to_send;
    io_operation_t operation;  // OP_ACCEPT, OP_RECV, OP_SEND
    struct client_context_t *next;
} client_context_t;
```

### 3. Threading Systems (`thread_pool.c`)

#### Windows 9x Implementation: Semaphore-Based Thread Pool

**Architecture**:
```
Listen Thread ──┐
                │
                ▼
        ┌──────────────┐
        │ Work Queue   │◄─── Semaphore Control
        │ (Mutex)      │
        └──────────────┘
                │
                ▼
    ┌─────────────────────────┐
    │ Worker Threads (4)      │
    │ ┌─────┐ ┌─────┐ ┌─────┐ │
    │ │  T1 │ │  T2 │ │ ... │ │
    │ └─────┘ └─────┘ └─────┘ │
    └─────────────────────────┘
```

**Synchronization**:
- **Semaphore**: Signals available work items
- **Mutex**: Protects work queue access
- **Event**: Coordinates shutdown

**Flow**:
1. Main thread accepts connections
2. Creates client context
3. Queues work item to thread pool
4. Worker thread processes HTTP request/response
5. Closes connection and cleans up

#### Windows NT Implementation: I/O Completion Ports (IOCP)

**Architecture**:
```
                ┌─────────────────┐
                │ Completion Port │
                └─────────────────┘
                         │
        ┌────────────────┼────────────────┐
        │                │                │
        ▼                ▼                ▼
   ┌─────────┐    ┌─────────────┐   ┌─────────┐
   │Worker T1│    │ Worker T2   │   │Worker T3│
   └─────────┘    └─────────────┘   └─────────┘
        │                │                │
        └────────────────┼────────────────┘
                         │
                ┌─────────────────┐
                │ Overlapped I/O  │
                │ AcceptEx        │
                │ WSARecv         │
                │ WSASend         │
                └─────────────────┘
```

**Async Operations**:
1. **AcceptEx**: Non-blocking connection acceptance
2. **WSARecv**: Asynchronous data reception  
3. **WSASend**: Asynchronous data transmission

**Flow**:
1. Server posts multiple `AcceptEx` operations
2. Client connects → AcceptEx completes
3. Worker thread handles completion
4. Posts `WSARecv` for request data
5. Processes HTTP request asynchronously
6. Posts `WSASend` for response
7. Closes connection after send completion
8. Posts new `AcceptEx` to maintain pool

### 4. HTTP Processing (`server_http.c`)

**Purpose**: HTTP/1.0 protocol parsing and response generation.

**Parser Architecture**:
```c
enum http_method_t {
    HTTP_GET = 0,
    HTTP_POST = 1,
    HTTP_PUT = 2,
    HTTP_PATCH = 3,
    HTTP_UNKNOWN = 4
};

struct http_request_t {
    enum http_method_t method;
    char *path;
    char *version;
    // Headers and body parsing
};
```

**Processing Flow**:
1. Parse HTTP method and path
2. Validate request format
3. Route to appropriate handler
4. Generate standardized response
5. Log request details

### 5. Logging System (`logging.c`)

**Purpose**: Thread-safe logging with GUI integration.

**Architecture**:
```
Worker Threads ──┐
                 │
Thread Pool ─────┼──► log_printf() ──► SendNotifyMessage()
                 │                                │
IOCP Threads ────┘                                │
                                                  ▼
                                        ┌─────────────────┐
                                        │ Main GUI Thread │
                                        │ Updates ListBox │
                                        └─────────────────┘
```

**Thread Safety**:
- Uses `SendNotifyMessage()` for cross-thread communication
- No blocking between worker threads and GUI
- Separate log streams for server events and HTTP requests

**Message Types**:
- `W32HTTP_SERVERLOG`: Server status, connections, errors
- `W32HTTP_RESPONSELOG`: HTTP request/response details

### 6. Platform Detection (`platform_detect.c`)

**Purpose**: Runtime detection of Windows capabilities.

**Detection Logic**:
```c
int detect_platform(platform_info_t *info) {
    OSVERSIONINFO osvi;
    GetVersionEx(&osvi);
    
    if (osvi.dwPlatformId == VER_PLATFORM_WIN32_WINDOWS) {
        info->type = PLATFORM_WIN9X;  // Win95/98/ME
    } else if (osvi.dwPlatformId == VER_PLATFORM_WIN32_NT) {
        info->type = PLATFORM_WINNT;  // NT/2000/XP/Vista/7/8/10/11
    }
}
```

**Capability Determination**:
- **IOCP Support**: Available on NT 4.0+
- **AcceptEx**: Loaded dynamically via `WSAIoctl()`
- **Thread Pool**: Fallback for Win9x systems

## Multi-Client Architecture

### Windows 9x: Thread Pool Model

**Advantages**:
- Compatible with Windows 95/98/ME
- Simple and robust design
- Predictable resource usage

**Limitations**:
- Limited scalability (thread-per-client model)
- Higher memory overhead per connection
- Context switching overhead

**Optimal Use Cases**:
- Legacy system support
- Low to moderate connection counts (< 50 concurrent)
- Systems with limited memory

### Windows NT: IOCP Model

**Advantages**:
- Highly scalable (thousands of concurrent connections)
- Efficient CPU utilization
- Minimal thread overhead
- True asynchronous I/O

**Technical Details**:
- **AcceptEx**: Pre-posts accept operations
- **Overlapped I/O**: Non-blocking socket operations
- **Completion-based**: Event-driven processing
- **Memory Efficient**: Single buffer per connection

**Optimal Use Cases**:
- High-performance scenarios
- Many concurrent connections (100+ concurrent)
- Modern Windows systems (NT 4.0+)

## Configuration and Extensibility

### Server Configuration
```c
struct server_config_t {
    char *listen_addr;      // Bind address
    u16 listen_port;        // Listen port
    u32 max_clients;        // Connection limit
    u32 worker_threads;     // Thread pool size
    
    // Platform-specific threading
    platform_info_t platform_info;
    union {
        thread_pool_t thread_pool;  // Win9x
        iocp_pool_t iocp_pool;      // NT
    } threading;
};
```

### Compilation Targets

**Supported Compilers**:
- Microsoft Visual C++ 6.0+ 
- Open Watcom C/C++
- MinGW (with limitations)

**Build Systems**:
- CMake (primary)
- Visual Studio project files (.dsp, .vcproj, .sln)
- Watcom project files (.wpj, .tgt)

**C89 Compliance**:
- All variable declarations at function start
- No C99/C11 features
- Compatible with 16-bit and 32-bit compilers

## Performance Characteristics

### Memory Usage
- **Base Application**: ~30KB executable
- **Per-Connection (Win9x)**: ~8KB (thread stack + context)
- **Per-Connection (NT)**: ~8KB (context only)
- **GUI Resources**: Minimal (native controls)

### Threading Models
- **Win9x**: 1 listener + N worker threads (default: 4)
- **NT**: 1 listener + N IOCP workers (default: 4)
- **Connection Limit**: Configurable (default: 10)

### Scalability
- **Win9x**: ~50 concurrent connections (memory limited)
- **NT**: 1000+ concurrent connections (IOCP limited)
- **Latency**: Sub-millisecond response times for simple requests

## Security and Robustness

### Input Validation
- HTTP request parsing with bounds checking
- Buffer overflow protection
- Malformed request handling

### Resource Management
- Automatic connection cleanup
- Graceful shutdown procedures
- Memory leak prevention

### Error Handling
- Comprehensive error logging
- Graceful degradation
- Recovery from network failures

## Future Enhancements

### Planned Features
- **HTTP/1.1 Compliance**: Keep-alive, chunked encoding
- **TLS Support**: SSL/TLS 1.2 with mbedTLS
- **JSON Processing**: Built-in JSON parser
- **Dynamic Handlers**: DLL-based request routing
- **Service Mode**: Windows Service capability

### Extensibility Points
- Pluggable HTTP handlers
- Custom authentication modules
- Request/response middleware
- Protocol extensions

## Development Guidelines

### Code Style
- Pure C (no C++)
- Windows API naming conventions
- Explicit error handling
- Conservative memory management

### Testing Approach
- Manual testing on target platforms
- Network load testing
- Memory leak detection
- Cross-compiler validation

### Debugging
- Extensive logging infrastructure
- GUI-integrated diagnostics
- Real-time connection monitoring
- Platform-specific debugging tools

This architecture demonstrates how to build efficient, portable network applications using platform-specific optimizations while maintaining compatibility across decades of Windows evolution.