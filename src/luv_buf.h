#ifndef _LUV_BUF_H_
#define _LUV_BUF_H_

typedef struct luv_buf_t {
  size_t   size;
  uint8_t* head;
  uint8_t* base;
} luv_buf_t;

luv_buf_t* luvL_buf_new(size_t size);

void luvL_buf_need (luv_buf_t* buf, size_t len);
void luvL_buf_init (luv_buf_t* buf, uint8_t* data, size_t len);
void luvL_buf_free (luv_buf_t* buf);

void luvL_buf_put (luv_buf_t* buf, uint8_t val);
void luvL_buf_write (luv_buf_t* buf, uint8_t* data, size_t len);
void luvL_buf_write_uleb128 (luv_buf_t* buf, uint32_t val);

uint8_t  luvL_buf_get (luv_buf_t* buf);
uint8_t  luvL_buf_peek (luv_buf_t* buf);
uint8_t* luvL_buf_read (luv_buf_t* buf, size_t len);
uint32_t luvL_buf_read_uleb128 (luv_buf_t* buf);

int luvL_writer(lua_State* L, const char* str, size_t len, void* buf);

#define /* _LUV_BUF_H_ */
