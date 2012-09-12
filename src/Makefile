LIBNAME = luv
LUADIR = /usr/include/lua5.1/

COPT = -O2 -DNDEBUG -fPIC

CWARNS = -Wall

OS_NAME=$(shell uname -s)

CFLAGS = $(CWARNS) $(COPT) -I$(LUADIR) -I./include -I./include/uv-private -I../deps/luajit/src
CC = gcc

ifeq ($(OS_NAME), Darwin)
DLLFLAGS=-undefined dynamic_lookup -ldl -lpthread -framework Carbon -framework CoreServices
else
DLLFLAGS=-shared
ENV= 
endif

SRCS := $(wildcard *.c)
OBJS := $(patsubst %.c,%.o,$(wildcard *.c))

all : luv.so

$(OBJS) : 
	$(CC) -c $(CFLAGS) $(SRCS)

luv.so : $(OBJS)
	$(CC) $(DLLFLAGS) $(OBJS) uv.a -o luv.so

clean:
	rm -f *.o *.so *.a
