#
# configuration
#
# comment out to not include zmq. may be useful for embedded devices
USE_ZMQ := 1

##########

LUADIR = /usr/local/include/luajit-2.0

COPT = -O2 -fPIC

CWARNS = -Wall -Werror

CFLAGS += $(CWARNS) $(COPT) -I$(LUADIR) -I./uv/include
ifdef USE_ZMQ
CFLAGS += -I./zmq/include
endif

OS_NAME = $(shell uname -s)
MH_NAME = $(shell uname -m)

LDFLAGS += -lm -ldl -lpthread

AR = ar

ifeq ($(OS_NAME), Darwin)
LDFLAGS += -bundle -undefined dynamic_lookup -framework CoreServices
ifeq ($(MH_NAME), x86_64)
endif
else
LDFLAGS += -shared -lrt
ifdef USE_ZMQ
LDFLAGS += -lstdc++
endif
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
	luv_process.c
ifdef USE_ZMQ
CFLAGS += -DUSE_ZMQ
SRCS += luv_zmq.c
endif
OBJS := $(patsubst %.c,%.o,$(SRCS))

LIBS = uv/libuv.a
ifdef USE_ZMQ
LIBS += zmq/src/.libs/libzmq.a
endif

all: luv.so libluv.a
deps: $(LIBS)

luv.so: $(OBJS) $(LIBS)
	$(CC) -o $@ $^ $(LDFLAGS)

libluv.a: $(OBJS) $(LIBS)
	$(AR) -rcs $@ $^

$(OBJS):
	$(CC) -c $(CFLAGS) $(SRCS)

uv/libuv.a:
	$(MAKE) CFLAGS="-fPIC" -C ./uv

zmq/src/.libs/libzmq.a: zmq/Makefile
	$(MAKE) CXXFLAGS="-fPIC" -C ./zmq

zmq/Makefile:
	# N.B. this requires auto* tools which is PITA for embedded systems
	cd ./zmq && ./autogen.sh && ./configure --disable-shared

clean:
	rm -f *.o *.so *.a

realclean: clean
	$(MAKE) -C ./uv clean
	$(MAKE) -C ./zmq clean

.PHONY: all clean realclean deps
.SILENT:
