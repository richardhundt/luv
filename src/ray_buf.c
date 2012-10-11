
#include <stdint.h>
#include <stddef.h>

#include "ray_buf.h"

int rayL_writer(lua_State* L, const char* str, size_t len, void* buf) {
  (void)L;
  rayL_buf_write((ray_buf_t*)buf, (uint8_t*)str, len);
  return 0;
}

ray_buf_t* rayL_buf_new(size_t size) {
  if (!size) size = 128;
  ray_buf_t* buf = (ray_buf_t*)malloc(sizeof(ray_buf_t));
  buf->base = (uint8_t*)malloc(size);
  buf->size = size;
  buf->head = buf->base;
  return buf;
}

void rayL_buf_free(ray_buf_t* buf) {
  free(buf->base);
  free(buf);
}

void rayL_buf_need(ray_buf_t* buf, size_t len) {
  size_t size = buf->size;
  if (!size) {
    size = 128;
    buf->base = (uint8_t*)malloc(size);
    buf->size = size;
    buf->head = buf->base;
  }
  ptrdiff_t head = buf->head - buf->base;
  ptrdiff_t need = head + len;
  while (size < need) size *= 2;
  if (size > buf->size) {
    buf->base = (uint8_t*)realloc(buf->base, size);
    buf->size = size;
    buf->head = buf->base + head;
  }
}
void rayL_buf_init_data(ray_buf_t* buf, uint8_t* data, size_t len) {
  rayL_buf_need(buf, len);
  memcpy(buf->base, data, len);
  buf->head += len;
}

void rayL_buf_put(ray_buf_t* buf, uint8_t val) {
  rayL_buf_need(buf, 1);
  *(buf->head++) = val;
}
void rayL_buf_write(ray_buf_t* buf, uint8_t* data, size_t len) {
  rayL_buf_need(buf, len);
  memcpy(buf->head, data, len);
  buf->head += len;
}
void rayL_buf_write_uleb128(ray_buf_t* buf, uint32_t val) {
  rayL_buf_need(buf, 5);
  size_t   n = 0;
  uint8_t* p = buf->head;
  for (; val >= 0x80; val >>= 7) {
    p[n++] = (uint8_t)((val & 0x7f) | 0x80);
  }
  p[n++] = (uint8_t)val;
  buf->head += n;
}

uint8_t rayL_buf_get(ray_buf_t* buf) {
  return *(buf->head++);
}
uint8_t* rayL_buf_read(ray_buf_t* buf, size_t len) {
  uint8_t* p = buf->head;
  buf->head += len;
  return p;
}
uint32_t rayL_buf_read_uleb128(ray_buf_t* buf) {
  const uint8_t* p = (const uint8_t*)buf->head;
  uint32_t v = *p++;
  if (v >= 0x80) {
    int sh = 0;
    v &= 0x7f;
    do {
     v |= ((*p & 0x7f) << (sh += 7));
    } while (*p++ >= 0x80);
  }
  buf->head = (uint8_t*)p;
  return v;
}
uint8_t rayL_buf_peek(ray_buf_t* buf) {
  return *buf->head;
}


