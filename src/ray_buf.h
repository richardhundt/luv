#ifndef _RAY_BUF_H_
#define _RAY_BUF_H_

typedef struct ray_buf_t {
  size_t   size;
  uint8_t* head;
  uint8_t* base;
} ray_buf_t;

ray_buf_t* rayL_buf_new(size_t size);

void rayL_buf_need (ray_buf_t* buf, size_t len);
void rayL_buf_init (ray_buf_t* buf, uint8_t* data, size_t len);
void rayL_buf_free (ray_buf_t* buf);

void rayL_buf_put (ray_buf_t* buf, uint8_t val);
void rayL_buf_write (ray_buf_t* buf, uint8_t* data, size_t len);
void rayL_buf_write_uleb128 (ray_buf_t* buf, uint32_t val);

uint8_t  rayL_buf_get (ray_buf_t* buf);
uint8_t  rayL_buf_peek (ray_buf_t* buf);
uint8_t* rayL_buf_read (ray_buf_t* buf, size_t len);
uint32_t rayL_buf_read_uleb128 (ray_buf_t* buf);

int rayL_writer(lua_State* L, const char* str, size_t len, void* buf);

#define /* _RAY_BUF_H_ */
