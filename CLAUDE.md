# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a Windows 32-bit HTTP server written in pure C using the Windows API. The project is designed to be a lightweight, minimal HTTP server that can run on Windows 95 OSR2 through Windows 11, with backward compatibility being a core design constraint.

## Build System

The project uses CMake as the primary build system:

```bash
# Build the project
mkdir build && cd build
cmake .. 
cmake --build .
```

Alternative build systems are also supported:
- Visual Studio 6 project files (`.dsp`, `.dsw`)
- Visual Studio 2005 project files (`.vcproj`, `.sln`)
- Watcom project files (`.wpj`, `.tgt`)

## Architecture

The codebase is structured as a single-threaded GUI application with the following key components:

### Core Architecture
- **Main Window** (`main_window.c`): Windows GUI application entry point and main window management
- **Server Core** (`server_win32.c`): Socket server implementation with Windows-specific threading
- **HTTP Protocol** (`server_http.c`): HTTP request parsing and response generation
- **Logging System** (`logging.c`): Thread-safe logging that communicates with the GUI via Windows messages

### Key Design Patterns
- **Message-based threading**: Server communicates with GUI using `SendNotifyMessage()` and custom Windows messages
- **Event synchronization**: Uses Windows events (`CreateEvent`) for thread coordination
- **Single client model**: Currently serves one client at a time (enhancement planned for multi-threading)

### Important Headers
- `common.h`: Platform compatibility layer with compiler-specific type definitions
- `server.h`: Server configuration structure
- `server_http.h`: HTTP protocol definitions and parsing functions
- `logging.h`: Logging system interface

## Development Constraints

### Compatibility Requirements
- Must run on Windows 95 OSR2 and Windows NT 4.0
- Uses Winsock2 for networking
- Compiler support: MSVC, Watcom, MinGW
- No modern C runtime dependencies

### Code Style
- Pure C (no C++)
- Windows API calls with explicit error handling
- Conservative memory management
- No dynamic allocation for core server operations

## Multi-Client Implementation

The server now supports multiple concurrent clients with platform-specific optimizations:

### Windows 9x Implementation
- **Thread Pool**: Uses semaphore-based thread pool for client handling
- **Worker Threads**: Configurable number of worker threads (default: 4)
- **Client Context**: Each client gets its own context structure for isolated processing
- **Thread Synchronization**: Uses Windows synchronization primitives (semaphores, mutexes, events)

### Windows NT Implementation  
- **IOCP Support**: Uses I/O Completion Ports for efficient asynchronous I/O
- **Scalable Architecture**: Designed to handle more concurrent connections efficiently
- **Platform Detection**: Automatic detection of Windows version to choose appropriate implementation

### Key Files
- `platform_detect.c/h`: Windows version detection and platform capabilities
- `thread_pool.c/h`: Thread pool implementation for both Win9x and NT platforms
- `server_win32.c`: Updated server core with multi-client support
- `server.h`: Extended server configuration structure

### Configuration Options
- `max_clients`: Maximum concurrent clients (default: 10)
- `worker_threads`: Number of worker threads (default: 4)
- Platform-specific threading system chosen automatically

## Current Limitations

- Basic HTTP/1.0 implementation
- No SSL/TLS support
- Hard-coded responses for most requests
- IOCP implementation uses basic synchronous processing (full async I/O would require additional complexity)

## Completed Enhancements

- ✅ Multiple client support using threads
- ✅ Windows 9x vs NT detection
- ✅ Thread pool with semaphore for Win9x
- ✅ IOCP thread pool foundation for Windows NT

## Remaining Planned Enhancements

- HTTP/1.0 compliance improvements
- TLS 1.2 with mbedTLS
- JSON parser
- Windows Service capability
- Dynamic DLL dispatch for HTTP handlers
- Cross-platform socket portability