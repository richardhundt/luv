
LUADIR = /usr/local/include/luajit-2.0

COPT = -O2 -fPIC

CWARNS = -Wall

CFLAGS = $(CWARNS) $(COPT) -I$(LUADIR) -I./uv/include -I./zmq/include

OS_NAME=$(shell uname -s)
MH_NAME=$(shell uname -m)

LDFLAGS=-lm -ldl -lpthread

AR = ar

ifeq ($(OS_NAME), Darwin)
LDFLAGS+=-bundle -undefined dynamic_lookup -framework CoreServices
ifeq ($(MH_NAME), x86_64)
endif
else
LDFLAGS+=-shared -lrt -lstdc++
endif

#SRCS := $(wildcard *.c)
#SRCS := ray.c ray_object.c ray_cond.c ray_core.c ray_fs.c ray_timer.c ray_stream.c ray_net.c ray_zmq.c ray_thread.c
SRCS := ray.c \
	ray_lib.c \
	ray_cond.c \
	ray_state.c \
	ray_fiber.c \
	ray_thread.c \
	ray_buf.c \
	ray_codec.c \
#	ray_object.c \
#	ray_timer.c \
#	ray_idle.c \
#	ray_fs.c \
#	ray_stream.c \
#	ray_pipe.c \
#	ray_net.c \
#	ray_process.c \
#	ray_zmq.c

OBJS := $(patsubst %.c,%.o,$(SRCS))

all: ray.so libray.a

ray.so: libs $(OBJS)
	$(CC) $(OBJS) ./uv/uv.a ./zmq/src/.libs/libzmq.a -o ray.so $(LDFLAGS)

libray.a: libs $(OBJS)
	$(AR) -rcs libray.a *.o ./uv/src/*.o ./uv/src/*/*.o ./uv/src/*/*/*.o ./zmq/src/*.o

$(OBJS) :
	$(CC) -c $(CFLAGS) $(SRCS)

libs: ./zmq/Makefile
	$(MAKE) CFLAGS="-fPIC" -C ./uv
	$(MAKE) CXXFLAGS="-fPIC" -C ./zmq

./zmq/Makefile:
	cd ./zmq && ./autogen.sh && ./configure

clean:
	rm -f *.o *.so *.a

realclean: clean
	$(MAKE) -C ./uv clean
	$(MAKE) -C ./zmq clean

.PHONY: all clean realclean

