
#include <stdint.h>
#include <stddef.h>

#include "ray_buffer.h"

int rayL_writer(lua_State* L, const char* str, size_t len, void* buf) {
  (void)L;
  rayL_buffer_write((ray_buffer_t*)buf, (uint8_t*)str, len);
  return 0;
}

ray_buffer_t* rayL_buffer_new(size_t size) {
  if (!size) size = 128;
  ray_buffer_t* buf = (ray_buffer_t*)malloc(sizeof(ray_buffer_t));
  buf->base = (uint8_t*)malloc(size);
  buf->size = size;
  buf->head = buf->base;
  return buf;
}

void rayL_buffer_free(ray_buffer_t* buf) {
  free(buf->base);
  free(buf);
}

void rayL_buffer_need(ray_buffer_t* buf, size_t len) {
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
void rayL_buffer_init_data(ray_buffer_t* buf, uint8_t* data, size_t len) {
  rayL_buffer_need(buf, len);
  memcpy(buf->base, data, len);
  buf->head += len;
}

void rayL_buffer_put(ray_buffer_t* buf, uint8_t val) {
  rayL_buffer_need(buf, 1);
  *(buf->head++) = val;
}
void rayL_buffer_write(ray_buffer_t* buf, uint8_t* data, size_t len) {
  rayL_buffer_need(buf, len);
  memcpy(buf->head, data, len);
  buf->head += len;
}
void rayL_buffer_write_uleb128(ray_buffer_t* buf, uint32_t val) {
  rayL_buffer_need(buf, 5);
  size_t   n = 0;
  uint8_t* p = buf->head;
  for (; val >= 0x80; val >>= 7) {
    p[n++] = (uint8_t)((val & 0x7f) | 0x80);
  }
  p[n++] = (uint8_t)val;
  buf->head += n;
}

uint8_t rayL_buffer_get(ray_buffer_t* buf) {
  return *(buf->head++);
}
uint8_t* rayL_buffer_read(ray_buffer_t* buf, size_t len) {
  uint8_t* p = buf->head;
  buf->head += len;
  return p;
}
uint32_t rayL_buffer_read_uleb128(ray_buffer_t* buf) {
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
uint8_t rayL_buffer_peek(ray_buffer_t* buf) {
  return *buf->head;
}


