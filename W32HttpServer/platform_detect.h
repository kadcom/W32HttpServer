#ifndef W32HTTP_PLATFORM_DETECT_H
#define W32HTTP_PLATFORM_DETECT_H

#include "common.h"

typedef enum {
    PLATFORM_WIN9X = 0,
    PLATFORM_WINNT = 1,
    PLATFORM_UNKNOWN = 2
} platform_type_t;

typedef struct {
    platform_type_t type;
    u32 major_version;
    u32 minor_version;
    u32 build_number;
    char version_string[128];
} platform_info_t;

int detect_platform(platform_info_t *info);
BOOL is_iocp_supported(void);

#endif // W32HTTP_PLATFORM_DETECT_H