
LUADIR = /usr/local/include/luajit-2.0

COPT = -O2 -fPIC

CWARNS = -Wall

CFLAGS = $(CWARNS) $(COPT) -I$(LUADIR) -I./uv/include -L./uv

OS_NAME=$(shell uname -s)
MH_NAME=$(shell uname -m)

LDFLAGS=-luv -lm -ldl -lpthread

AR = ar

ifeq ($(OS_NAME), Darwin)
LDFLAGS+=-bundle -undefined dynamic_lookup -framework CoreServices
ifeq ($(MH_NAME), x86_64)
endif
else
LDFLAGS+=-shared -lrt
endif

#SRCS := $(wildcard *.c)
SRCS := ray_lib.c \
 	ray_hash.c \
 	ray_list.c \
 	ray_cond.c \
	ray_queue.c \
	ray_state.c

OBJS := $(patsubst %.c,%.o,$(SRCS))

BASE := ray_lib.o \
	ray_hash.o \
	ray_list.o \
	ray_cond.o \
	ray_queue.o \
	ray_state.o

LIBS := ./uv/libuv.a

all: deps $(OBJS) ../ray/fiber.so ../ray/timer.so ../ray/net.so

../ray/fiber.so: $(OBJS)
	$(CC) $(BASE) $(LIBS) ray_fiber.c -o ../ray/fiber.so $(LDFLAGS)

../ray/timer.so: $(OBJS)
	$(CC) $(BASE) $(LIBS) ray_timer.c -o ../ray/timer.so $(LDFLAGS)

../ray/net.so: $(OBJS)
	$(CC) $(BASE) $(LIBS) ray_stream.c ray_net.c -o ../ray/net.so $(LDFLAGS)

$(OBJS):
	$(CC) -c $(CFLAGS) $(SRCS)

deps:
	$(MAKE) CFLAGS="-fPIC" -C ./uv

clean:
	rm -f *.o *.so

realclean: clean
	$(MAKE) -C ./uv clean

.PHONY: all clean realclean

