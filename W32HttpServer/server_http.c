#include "server_http.h"

struct http_method_map {
  const char *method_str; 
  enum http_method_t method_num;
};

static struct http_method_map method_map[] = {
  {"GET", HTTP_GET},
  {"POST", HTTP_POST},
  {"PUT", HTTP_PUT},
  {"PATCH", HTTP_PATCH},
};

static const size_t http_method_map_n = sizeof(method_map) / sizeof(struct http_method_map);

static BOOL ascii_compare (u8 c1, u8 c2) {
  return (c1 == c2 || (c1-32) == c2 || c1 == (c2-32)) ? TRUE : FALSE;
}

static BOOL nocase_compare(const char *str1, const char *str2, const size_t n) {
  u8 *ptr1 = (u8*)str1;
  u8 *ptr2 = (u8*)str2;
  size_t i; 
  
  if ( 0 == n ) {
    return FALSE;
  }
  

  for (i = 0; i < n; ++i, ++ptr1, ++ptr2) {
    if (!ascii_compare(*ptr1, *ptr2)) {
      return FALSE;
    }
  }

  return TRUE;
}

static enum http_method_t parse_method(const u8 *payload, u8 **path_ptr) {
  u8 *method_end;
  u32 compare_len;
  enum http_method_t method;
  size_t i;

  for(method_end = (u8*) payload; *method_end != ' '; ++method_end) {
    // just go through
  }

  if (' ' != *method_end ) {
    return -1;
  }

  compare_len = method_end - payload;

  for(i = 0; i < http_method_map_n; ++i) {
    if (strnicmp(method_map[i].method_str, (const char*)payload, compare_len) == 0) {
      method =  method_map[i].method_num;
	  break;
    }
  }

  if (NULL != path_ptr) {
    *path_ptr = method_end + 1; 
  }
  
  return method;
}

static struct http_path_t parse_path(const u8 *method_ptr, u8 **http_version_ptr) {
  struct http_path_t path = { 0, 0 }; 
  u8 *ptr;
  path.start = method_ptr;
  
  for(ptr = (u8 *) path.start; *ptr != ' '; ++ptr);
  path.end = ptr; 

  if (NULL != http_version_ptr) {
    *http_version_ptr = (u8*)path.end + 1;
  }
  
  return path;
}

int parse_http_request(
    u8 *payload, 
    size_t payload_len, 
    struct http_request_t *req_output)
{
  u8 *ptr, *next_ptr;
  if (NULL == req_output) {
    return -1; 
  }

  memset(req_output, 0, sizeof(struct http_request_t));

  req_output->payload = payload;
  req_output->payload_len = payload_len;
  req_output->method = parse_method(payload, &ptr);
  req_output->path = parse_path(ptr, &next_ptr);

  ptr = next_ptr;

  /* find header start */
  for (ptr = payload; *ptr != '\n'; ++ptr);
  req_output->header.start = ++ptr;

  /* find double newlines */
  for (; *(u32*)ptr != 0x0A0D0A0D; ++ptr); // 0x0A0D0A0D = "\r\n\r\n"
  req_output->header.end = ptr;

  return 0;
}

struct http_header_item_t find_header(struct http_header_t *hdr, const char *header_name) {
  size_t hdr_key_len = strlen(header_name);
  struct http_header_item_t item;
  int ret, i;
  const u8 *ptr;

  memset(&item, 0, sizeof(struct http_header_item_t));

  item.start = hdr->start;
  do {

    ret = strnicmp((const char*) item.start, header_name, hdr_key_len);

    if (ret != 0) {
       // skip the whole line
       for(ptr = item.start; *ptr != '\n'; ++ptr); // find '\r\n'
       item.start = ptr + 1;
       continue;
    }
    
    for(ptr = item.start; *ptr != ':'; ++ptr); // find ':'
    for(; *ptr == '\t' || *ptr == ' '; ++ptr); // skip spaces 
    item.value = ptr;
    for(; *ptr != '\n'; ++ptr); // find '\r\n'      item.end   = ptr - 1;
 
  } while (ret != 0 || ptr == hdr->end || ptr + 1 == hdr->end);

  if (item.value == 0) {
    item.start = 0;
  }

  return item;
}

/* File magic number signatures for common file types */
const char* get_mime_type_from_magic(const u8* file_data, u32 data_size) {
    u32 magic;
    u16 magic16;

    if (NULL == file_data || data_size < 4) {
        return NULL;
    }

    /* Assemble the signature words from individual bytes rather than casting
       to (u32*)/(u16*). A misaligned word read faults on ARM and other strict
       targets (this server must run from Win95 through Win11/ARM), and the
       byte order is fixed here so the constants below are valid everywhere. */
    magic16 = (u16)((u16)file_data[0] | ((u16)file_data[1] << 8));
    magic   = (u32)file_data[0]
            | ((u32)file_data[1] << 8)
            | ((u32)file_data[2] << 16)
            | ((u32)file_data[3] << 24);

    /* Image formats */
    if (magic == 0x474E5089) return "image/png";           /* PNG: 89 50 4E 47 */
    if (magic16 == 0xD8FF) return "image/jpeg";            /* JPEG: FF D8 */
    if (magic == 0x38464947) return "image/gif";           /* GIF87a/89a: GIF8 */
    if (magic == 0x002A4949 || magic == 0x2A004D4D) return "image/tiff"; /* TIFF */
    if (magic16 == 0x4D42) return "image/bmp";             /* BMP: BM */
    if (magic == 0x00000100) return "image/x-icon";        /* ICO */

    /* Document formats */
    if (magic == 0x46445025) return "application/pdf";     /* PDF: %PDF */
    if (magic == 0x504B0304 || magic == 0x504B0506) {      /* ZIP signature */
        /* Could be Office documents, but default to zip */
        return "application/zip";
    }

    /* Archive formats */
    if (magic16 == 0x8B1F) return "application/gzip";      /* GZIP: 1F 8B */
    if (magic == 0x21726172) return "application/x-rar";   /* RAR */
    if (magic == 0x04034B50) return "application/zip";     /* ZIP */
    
    /* Audio/Video formats */
    if (magic == 0x5367674F) return "application/ogg";     /* OGG */
    if (magic == 0x43614C66) return "audio/flac";          /* FLAC */
    if (magic16 == 0xFBFF || magic16 == 0xF3FF) return "audio/mpeg"; /* MP3 */
    if (data_size >= 8 && strncmp((char*)file_data + 4, "ftyp", 4) == 0) return "video/mp4"; /* MP4 */
    
    /* Text formats - check for common text patterns */
    if (data_size >= 5 && strncmp((char*)file_data, "<?xml", 5) == 0) return "text/xml";
    if (data_size >= 15 && strncmp((char*)file_data, "<!DOCTYPE html", 14) == 0) return "text/html";
    if (data_size >= 6 && strncmp((char*)file_data, "<html", 5) == 0) return "text/html";
    
    /* Check if it looks like text (printable ASCII) */
    if (data_size > 0) {
        u32 i;
        u32 printable_count = 0;
        u32 check_size = data_size > 512 ? 512 : data_size;
        
        for (i = 0; i < check_size; i++) {
            u8 c = file_data[i];
            if ((c >= 32 && c <= 126) || c == '\t' || c == '\n' || c == '\r') {
                printable_count++;
            }
        }
        
        /* If more than 95% printable characters, assume it's text */
        if (printable_count * 100 / check_size > 95) {
            return "text/plain";
        }
    }
    
    return NULL; /* Unknown type */
}

const char* get_mime_type_from_extension(const char* file_extension) {
    if (NULL == file_extension) {
        return "application/octet-stream";
    }
    
    /* Convert to lowercase for comparison */
    if (lstrcmpi(file_extension, ".html") == 0 || lstrcmpi(file_extension, ".htm") == 0) return "text/html";
    if (lstrcmpi(file_extension, ".css") == 0) return "text/css";
    if (lstrcmpi(file_extension, ".js") == 0) return "application/javascript";
    if (lstrcmpi(file_extension, ".json") == 0) return "application/json";
    if (lstrcmpi(file_extension, ".xml") == 0) return "text/xml";
    if (lstrcmpi(file_extension, ".txt") == 0) return "text/plain";
    
    /* Image formats */
    if (lstrcmpi(file_extension, ".png") == 0) return "image/png";
    if (lstrcmpi(file_extension, ".jpg") == 0 || lstrcmpi(file_extension, ".jpeg") == 0) return "image/jpeg";
    if (lstrcmpi(file_extension, ".gif") == 0) return "image/gif";
    if (lstrcmpi(file_extension, ".bmp") == 0) return "image/bmp";
    if (lstrcmpi(file_extension, ".ico") == 0) return "image/x-icon";
    if (lstrcmpi(file_extension, ".svg") == 0) return "image/svg+xml";
    
    /* Document formats */
    if (lstrcmpi(file_extension, ".pdf") == 0) return "application/pdf";
    if (lstrcmpi(file_extension, ".doc") == 0) return "application/msword";
    if (lstrcmpi(file_extension, ".docx") == 0) return "application/vnd.openxmlformats-officedocument.wordprocessingml.document";
    
    /* Archive formats */
    if (lstrcmpi(file_extension, ".zip") == 0) return "application/zip";
    if (lstrcmpi(file_extension, ".rar") == 0) return "application/x-rar";
    if (lstrcmpi(file_extension, ".gz") == 0) return "application/gzip";
    if (lstrcmpi(file_extension, ".tar") == 0) return "application/x-tar";
    
    /* Audio formats */
    if (lstrcmpi(file_extension, ".mp3") == 0) return "audio/mpeg";
    if (lstrcmpi(file_extension, ".wav") == 0) return "audio/wav";
    if (lstrcmpi(file_extension, ".ogg") == 0) return "audio/ogg";
    if (lstrcmpi(file_extension, ".flac") == 0) return "audio/flac";
    
    /* Video formats */
    if (lstrcmpi(file_extension, ".mp4") == 0) return "video/mp4";
    if (lstrcmpi(file_extension, ".avi") == 0) return "video/x-msvideo";
    if (lstrcmpi(file_extension, ".mov") == 0) return "video/quicktime";
    if (lstrcmpi(file_extension, ".wmv") == 0) return "video/x-ms-wmv";
    
    return "application/octet-stream";
}

const char* detect_mime_type(const char* file_path, const u8* file_data, u32 data_size) {
    const char* mime;
    const char* file_ext;

    /* Prefer the file extension when it maps to a known type. Content sniffing
       classifies any printable file as text/plain, which would mislabel .css
       and .js and stop browsers from applying them; the extension is
       authoritative for those, so it must win. */
    if (file_path != NULL) {
        /* Find last occurrence of '.' manually */
        file_ext = NULL;
        {
            const char* p = file_path;
            while (*p) {
                if (*p == '.') {
                    file_ext = p;
                }
                p++;
            }
        }
        mime = get_mime_type_from_extension(file_ext);
        if (lstrcmp(mime, "application/octet-stream") != 0) {
            return mime;
        }
    }

    /* Unknown or missing extension: fall back to magic-number sniffing. */
    mime = get_mime_type_from_magic(file_data, data_size);
    if (mime != NULL) {
        return mime;
    }

    return "application/octet-stream";
}

int build_http_error_response(int status_code, const char* message, char* response_buffer, u32 buffer_size, u32* response_length) {
    const char* status_text;
    int len;
    
    if (NULL == message || NULL == response_buffer || NULL == response_length || buffer_size == 0) {
        return -1;
    }
    
    switch (status_code) {
        case 400: status_text = "Bad Request"; break;
        case 403: status_text = "Forbidden"; break;
        case 404: status_text = "Not Found"; break;
        case 405: status_text = "Method Not Allowed"; break;
        case 500: status_text = "Internal Server Error"; break;
        case 501: status_text = "Not Implemented"; break;
        default: status_text = "Error"; break;
    }
    
    len = wsprintf(response_buffer,
        "HTTP/1.0 %d %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n"
        "<html><head><title>%d %s</title></head>"
        "<body><h1>%d %s</h1><p>%s</p></body></html>",
        status_code, status_text,
        (u32)lstrlen(message) + 100, /* Approximate HTML wrapper size */
        status_code, status_text,
        status_code, status_text, message);
    
    if (len < 0 || (u32)len >= buffer_size) {
        return -1;
    }
    
    *response_length = (u32)len;
    return 0;
}

int build_http_file_header(const char* mime_type, u32 file_size, char* response_buffer, u32 buffer_size, u32* header_length) {
    int len;

    if (NULL == mime_type || NULL == response_buffer || NULL == header_length || buffer_size == 0) {
        return -1;
    }

    /* Headers only - the body is streamed from disk after this. Content-Length
       is known up front (file size) so HTTP/1.0 clients work and no chunked
       transfer-encoding is needed. */
    len = wsprintf(response_buffer,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %u\r\n"
        "Connection: close\r\n"
        "\r\n",
        mime_type, file_size);

    if (len < 0 || (u32)len >= buffer_size) {
        return -1;
    }

    *header_length = (u32)len;
    return 0;
}

int build_http_redirect_response(const char* location, char* response_buffer, u32 buffer_size, u32* response_length) {
    int len;

    if (NULL == location || NULL == response_buffer || NULL == response_length || buffer_size == 0) {
        return -1;
    }

    len = wsprintf(response_buffer,
        "HTTP/1.0 301 Moved Permanently\r\n"
        "Location: %s\r\n"
        "Content-Type: text/html\r\n"
        "Content-Length: 0\r\n"
        "Connection: close\r\n"
        "\r\n",
        location);

    if (len < 0 || (u32)len >= buffer_size) {
        return -1;
    }

    *response_length = (u32)len;
    return 0;
}

void url_decode(const char* src, char* dest, u32 dest_size) {
    const char* p;
    char* d;
    u32 written;
    
    if (NULL == src || NULL == dest || dest_size == 0) {
        return;
    }
    
    p = src;
    d = dest;
    written = 0;
    
    while (*p && written < dest_size - 1) {
        if (*p == '%' && p[1] && p[2]) {
            /* Decode %XX hex encoding */
            int hex_val;
            char hex_str[3] = {p[1], p[2], '\0'};
            hex_val = 0;
            if (hex_str[0] >= '0' && hex_str[0] <= '9') hex_val = (hex_str[0] - '0') * 16;
            else if (hex_str[0] >= 'A' && hex_str[0] <= 'F') hex_val = (hex_str[0] - 'A' + 10) * 16;
            else if (hex_str[0] >= 'a' && hex_str[0] <= 'f') hex_val = (hex_str[0] - 'a' + 10) * 16;
            
            if (hex_str[1] >= '0' && hex_str[1] <= '9') hex_val += (hex_str[1] - '0');
            else if (hex_str[1] >= 'A' && hex_str[1] <= 'F') hex_val += (hex_str[1] - 'A' + 10);
            else if (hex_str[1] >= 'a' && hex_str[1] <= 'f') hex_val += (hex_str[1] - 'a' + 10);
            
            if (hex_val >= 0 && hex_val <= 255) {
                *d++ = (char)hex_val;
                p += 3;
            } else {
                *d++ = *p++;
            }
        } else if (*p == '+') {
            /* Convert + to space */
            *d++ = ' ';
            p++;
        } else {
            *d++ = *p++;
        }
        written++;
    }
    
    *d = '\0';
}

int is_safe_path(const char* path) {
    const char* p;
    
    if (NULL == path) {
        return 0;
    }
    
    /* Check for directory traversal attempts */
    p = path;
    while (*p) {
        if (*p == '.' && *(p+1) == '.') {
            return 0;
        }
        p++;
    }
    
    /* Check for absolute paths on Windows */
    if (path[0] == '\\' || (path[1] == ':' && path[0] != '\0')) {
        return 0;
    }
    
    /* Check for invalid characters */
    for (p = path; *p; p++) {
        if (*p == '<' || *p == '>' || *p == '|' || *p == '"' || *p == '*' || *p == '?') {
            return 0;
        }
    }
    
    return 1;
}

int resolve_file_path(const char* document_root, const char* url_path, char* full_path, u32 path_buffer_size) {
    char decoded_path[MAX_PATH];
    const char* clean_url_path;
    
    if (NULL == document_root || NULL == url_path || NULL == full_path || path_buffer_size == 0) {
        return -1;
    }
    
    /* URL decode the path */
    url_decode(url_path, decoded_path, sizeof(decoded_path));
    
    /* Skip leading slash */
    clean_url_path = decoded_path;
    if (clean_url_path[0] == '/') {
        clean_url_path++;
    }
    
    /* Check for path safety */
    if (!is_safe_path(clean_url_path)) {
        return -1;
    }
    
    /* If empty path, serve index.html */
    if (clean_url_path[0] == '\0') {
        clean_url_path = "index.html";
    }
    
    /* Build full path */
    if (wsprintf(full_path, "%s\\%s", document_root, clean_url_path) >= (int)path_buffer_size) {
        return -1; /* Path too long */
    }
    
    return 0;
}

/* Build the response headers for a streamed directory listing.
 *
 * Unlike a file, a generated listing has no length we can know in advance
 * without walking the whole directory first. Rather than buffer the entire
 * page (which would re-introduce the size cap we are trying to remove), we
 * deliberately OMIT Content-Length and rely on "Connection: close" to mark the
 * end of the body: the server streams the HTML and then closes the socket, and
 * the client reads until end-of-stream. This is the classic HTTP/1.0 way of
 * delivering dynamic content and works with every client from Win95 onward.
 * The actual <li> rows are produced incrementally by the directory generator
 * in server_win32.c. */
int build_http_listing_header(char* response_buffer, u32 buffer_size, u32* header_length) {
    int len;

    if (NULL == response_buffer || NULL == header_length || buffer_size == 0) {
        return -1;
    }

    len = wsprintf(response_buffer,
        "HTTP/1.0 200 OK\r\n"
        "Content-Type: text/html\r\n"
        "Connection: close\r\n"   /* no Content-Length: body ends when we close */
        "\r\n");

    if (len < 0 || (u32)len >= buffer_size) {
        return -1;
    }

    *header_length = (u32)len;
    return 0;
}


