
#include <stdint.h>
#include <stddef.h>

#include "luv_buf.h"

int luvL_writer(lua_State* L, const char* str, size_t len, void* buf) {
  (void)L;
  luvL_buf_write((luv_buf_t*)buf, (uint8_t*)str, len);
  return 0;
}

luv_buf_t* luvL_buf_new(size_t size) {
  if (!size) size = 128;
  luv_buf_t* buf = (luv_buf_t*)malloc(sizeof(luv_buf_t));
  buf->base = (uint8_t*)malloc(size);
  buf->size = size;
  buf->head = buf->base;
  return buf;
}

void luvL_buf_free(luv_buf_t* buf) {
  free(buf->base);
  free(buf);
}

void luvL_buf_need(luv_buf_t* buf, size_t len) {
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
void luvL_buf_init_data(luv_buf_t* buf, uint8_t* data, size_t len) {
  luvL_buf_need(buf, len);
  memcpy(buf->base, data, len);
  buf->head += len;
}

void luvL_buf_put(luv_buf_t* buf, uint8_t val) {
  luvL_buf_need(buf, 1);
  *(buf->head++) = val;
}
void luvL_buf_write(luv_buf_t* buf, uint8_t* data, size_t len) {
  luvL_buf_need(buf, len);
  memcpy(buf->head, data, len);
  buf->head += len;
}
void luvL_buf_write_uleb128(luv_buf_t* buf, uint32_t val) {
  luvL_buf_need(buf, 5);
  size_t   n = 0;
  uint8_t* p = buf->head;
  for (; val >= 0x80; val >>= 7) {
    p[n++] = (uint8_t)((val & 0x7f) | 0x80);
  }
  p[n++] = (uint8_t)val;
  buf->head += n;
}

uint8_t luvL_buf_get(luv_buf_t* buf) {
  return *(buf->head++);
}
uint8_t* luvL_buf_read(luv_buf_t* buf, size_t len) {
  uint8_t* p = buf->head;
  buf->head += len;
  return p;
}
uint32_t luvL_buf_read_uleb128(luv_buf_t* buf) {
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
uint8_t luvL_buf_peek(luv_buf_t* buf) {
  return *buf->head;
}


