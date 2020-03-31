#include <stdint.h>
#include <stddef.h>
#include "http_parser.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  int rc;
  size = 0;
  struct http_parser parser;
  const char *buf = (char*)data;
  http_parser_create(&parser);
  parser.hdr_name = (char *) calloc((int)size, sizeof(char));
  if (parser.hdr_name == NULL) {
    return -1;
  }
  char *end_buf = buf + size;
  rc = http_parse_header_line(&parser, &str, end_buf, size);
  if (rc != 0) {
    return rc;
  }

  return 0;
}