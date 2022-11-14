#ifndef W32HTTP_COMMON_H
#define W32HTTP_COMMON_H

#pragma once

#ifndef _WINSOCKAPI_
#define _WINSOCKAPI_
#endif

// #define MODERN_SOCKET 0

#define _WIN32_WINNT 0x400
#include <Windows.h>
#include <Winsock2.h>
#include <Ws2tcpip.h>
#include <stdio.h>

#ifndef snprintf
#define snprintf _snprintf
#endif

#if defined(_MSC_VER) 
typedef unsigned __int8		u8;
typedef unsigned __int16	u16;
typedef unsigned __int32    u32;
typedef unsigned __int64	u64;
#elif defined(GNUC)
#include <stdint.h>
typedef uint8_t		u8;
typedef uint16_t	u16;
typedef uint32_t	u32;
typedef uint64_t	u64;
#else
#error "Incompatible compiler, use MSVC!"
#endif

typedef u8 utf8;

#include "resource.h"
#endif
