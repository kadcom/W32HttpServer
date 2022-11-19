#ifndef W32HTTP_HTTP_H
#define W32HTTP_HTTP_H
#include "common.h"

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

struct http_request_t {
  u32 status;
  u32 method; 

  struct http_header_t header;
  struct http_path_t path;

  u8 *payload;
  size_t payload_len;
};

size_t find_header(struct http_header_t *hdr, const char *header_name, const char *buf, const size_t buf_len);
int parse_http_request(u8 *payload, size_t payload_len, struct http_request_t *req_output);

#endif // 
