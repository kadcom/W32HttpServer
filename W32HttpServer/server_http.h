#ifndef W32HTTP_HTTP_H
#define W32HTTP_HTTP_H
#include "common.h"

#define MAXIMUM_HEADER_SIZE (8 * 1024);

enum http_method_t {
  HTTP_UNKNOWN = 0,
  HTTP_GET,
  HTTP_POST,
  HTTP_PATCH,
  HTTP_PUT,
};

struct http_header_t {
  const u8 *start;
  const u8 *end;
};

struct http_path_t {
  const u8 *start; 
  const u8 *end;
};

struct http_header_item_t {
  const u8 *start;
  const u8 *end;
  const u8 *value;
  const u8 *_next; 
};

struct http_request_t {
  u32 status;
  u32 method; 

  struct http_header_t header;
  struct http_path_t path;

  u8 *payload;
  size_t payload_len;
};

struct http_header_item_t find_header(struct http_header_t *hdr, const char *header_name);
int parse_http_request(u8 *payload, size_t payload_len, struct http_request_t *req_output);

/* File serving functions - caller provides all buffers */
const char* detect_mime_type(const char* file_path, const u8* file_data, u32 data_size);
const char* get_mime_type_from_magic(const u8* file_data, u32 data_size);
const char* get_mime_type_from_extension(const char* file_extension);
int read_file_to_buffer(const char* file_path, char* file_buffer, u32 buffer_size, u32* bytes_read);
int build_http_file_response(const char* file_path, const char* mime_type, const char* file_data, u32 file_size, char* response_buffer, u32 buffer_size, u32* response_length);
int build_http_file_header(const char* mime_type, u32 file_size, char* response_buffer, u32 buffer_size, u32* header_length);
int build_http_redirect_response(const char* location, char* response_buffer, u32 buffer_size, u32* response_length);
int build_http_error_response(int status_code, const char* message, char* response_buffer, u32 buffer_size, u32* response_length);
int build_http_listing_header(char* response_buffer, u32 buffer_size, u32* header_length);
void url_decode(const char* src, char* dest, u32 dest_size);
int is_safe_path(const char* path);
int resolve_file_path(const char* document_root, const char* url_path, char* full_path, u32 path_buffer_size);

#endif // 
