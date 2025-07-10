#include "platform_detect.h"
#include <strsafe.h>

int detect_platform(platform_info_t *info) {
    OSVERSIONINFO osvi;
    
    if (NULL == info) {
        return -1;
    }

    ZeroMemory(&osvi, sizeof(OSVERSIONINFO));
    osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
    
    if (!GetVersionEx(&osvi)) {
        return -1;
    }

    info->major_version = osvi.dwMajorVersion;
    info->minor_version = osvi.dwMinorVersion;
    info->build_number = osvi.dwBuildNumber;

    // Windows 9x family: Windows 95/98/ME
    if (VER_PLATFORM_WIN32_WINDOWS == osvi.dwPlatformId) {
        info->type = PLATFORM_WIN9X;
        if (osvi.dwMajorVersion == 4) {
            if (osvi.dwMinorVersion == 0) {
                StringCchCopy(info->version_string, sizeof(info->version_string), "Windows 95");
            } else if (osvi.dwMinorVersion == 10) {
                StringCchCopy(info->version_string, sizeof(info->version_string), "Windows 98");
            } else if (osvi.dwMinorVersion == 90) {
                StringCchCopy(info->version_string, sizeof(info->version_string), "Windows ME");
            } else {
                StringCchPrintf(info->version_string, sizeof(info->version_string), 
                    "Windows 9x (%d.%d)", osvi.dwMajorVersion, osvi.dwMinorVersion);
            }
        }
    }
    // Windows NT family: NT 4.0, 2000, XP, etc.
    else if (VER_PLATFORM_WIN32_NT == osvi.dwPlatformId) {
        info->type = PLATFORM_WINNT;
        if (osvi.dwMajorVersion == 4) {
            StringCchCopy(info->version_string, sizeof(info->version_string), "Windows NT 4.0");
        } else if (osvi.dwMajorVersion == 5) {
            if (osvi.dwMinorVersion == 0) {
                StringCchCopy(info->version_string, sizeof(info->version_string), "Windows 2000");
            } else if (osvi.dwMinorVersion == 1) {
                StringCchCopy(info->version_string, sizeof(info->version_string), "Windows XP");
            } else {
                StringCchPrintf(info->version_string, sizeof(info->version_string), 
                    "Windows NT 5.%d", osvi.dwMinorVersion);
            }
        } else {
            StringCchPrintf(info->version_string, sizeof(info->version_string), 
                "Windows NT %d.%d", osvi.dwMajorVersion, osvi.dwMinorVersion);
        }
    } else {
        info->type = PLATFORM_UNKNOWN;
        StringCchPrintf(info->version_string, sizeof(info->version_string), 
            "Unknown Platform (%d.%d)", osvi.dwMajorVersion, osvi.dwMinorVersion);
    }

    return 0;
}

BOOL is_iocp_supported(void) {
    platform_info_t info;
    
    if (detect_platform(&info) != 0) {
        return FALSE;
    }

    // IOCP is only supported on Windows NT 4.0 and later
    if (info.type == PLATFORM_WINNT && info.major_version >= 4) {
        return TRUE;
    }

    return FALSE;
}