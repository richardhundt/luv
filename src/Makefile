
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
#SRCS := luv.c luv_object.c luv_cond.c luv_core.c luv_fs.c luv_timer.c luv_stream.c luv_net.c luv_zmq.c luv_thread.c
SRCS := luv.c \
	luv_cond.c \
	luv_state.c \
	luv_fiber.c \
	luv_thread.c \
	luv_codec.c \
	luv_object.c \
	luv_timer.c \
	luv_idle.c \
	luv_fs.c \
	luv_stream.c \
	luv_pipe.c \
	luv_net.c \
	luv_process.c \
	luv_zmq.c
OBJS := $(patsubst %.c,%.o,$(SRCS))

all: luv.so libluv.a

luv.so: libs $(OBJS)
	$(CC) $(OBJS) ./uv/uv.a ./zmq/src/.libs/libzmq.a -o luv.so $(LDFLAGS)

libluv.a: libs $(OBJS)
	$(AR) -rcs libluv.a *.o ./uv/src/*.o ./uv/src/*/*.o ./uv/src/*/*/*.o ./zmq/src/*.o

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

