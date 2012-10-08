# NAME

Luv - Thermonuclear battery pack for Lua

# SYNOPSIS

local luv = require("luv")

# FEATURES

* scheduled fibers
* [libuv] TCP sockets
* [libuv] timers
* [libuv] filesystem operations
* [libuv] OS threads
* [libuv] pipes
* [zmq] ØMQ 3.x for the rest 
* binary serialization

# INSTALLATION

Run `make` and copy the luv.so to where you need it. In
theory both ØMQ and libuv support WIN32, but I have no
idea how that build system works there, so patches welcome.

# DESCRIPTION

Luv is an attempt to do libuv bindings to Lua in a style more
suited to a language with coroutines than edge-triggered
event-loop style programming with callbacks.

So how is it different?

At the heart of Luv is a reasonably fast coroutine scheduler
(20,000,000 context switches / second on my modest laptop).

Coroutines are wrapped as 'fibers' which add some extra bits
which allow them to be suspended and resumed by the libuv event
loop. This makes programming with them feel more like threads
with a nice linear flow, but without the impressive crashes.

Here's the canonical TCP echo server:

```Lua
local main = luv.fiber.create(function()
   local server = luv.net.tcp()
   server:bind("127.0.0.1", 8080)
   server:listen()

   while true do
      local client = luv.net.tcp()
      server:accept(client)

      local child = luv.fiber.create(function()
         while true do
            local got, str = client:read()
            if got then
               client:write("you said: "..str)
            else
               client:close()
               break
            end
         end
      end)

      -- put it in the ready queue
      child:ready()
   end
end)

main:join()
```

A key point is that fibers run at highest priority, so that a process
under I/O load gets doesn't starve the tasks which actually process
the data.

Once all pending fibers have been given a chance to run, the
event loop kicks in and polls for events and the wakes up
any suspended fibers waiting on events.

## States

A state is an execution context which can be suspended or resumed.
States can be either a thread - including the main thread - or a fiber.

States are not resumed immediately when ready, but are fair queued
to be resumed at the next possible time. Instead they are signalled
as being `ready`, typically by a libuv callback.

The semantics of suspending a state depends on whether it is a thread
or a fiber.

* Suspending a fiber is equivalent to removing it from the scheduing queue.
* Suspending a thread is equivalent to running the event loop and scheduler.

* Readying a fiber is equivalent to inserting it into the scheduling queue.
* Readying a thread is equivalent to interrupting the event loop.


## Fibers

Fibers are cooperatively scheduled plain Lua coroutines with one important
difference: when using I/O objects or timers, the scheduling is done for
you so that you don't explicitly need to call `coroutine.yield`.

This makes fibers more like green threads, but without preemption.
So most of the time, you just let them run and forget about the scheduling.

### luv.fiber.create(func, [arg1, ..., argN])

Fibers are created by calling `luv.fiber.create` and passing it the function
to be run inside the fiber, along with any additional arguments which are
passed to the function in turn.

NOTE: The fiber is *not* run until it is put in the ready queue and the main
thread is suspended. See `fiber:ready` and `fiber:join` below.

### fiber:ready()

Insert the fiber into the scheduler's ready queue. The scheduler is run
by the containing thread when it is suspended.

### fiber:join()

Inserts the fiber into the thread's scheduler and suspend the current
state until the fiber exits. Returns any values returned by the fiber.

### Fiber example:

```Lua
local f1 = luv.fiber.create(function(mesg)
    print("inside fiber, mesg: ", mesg)

    local f2 = luv.fiber.create(function()
        print("in child")
        return "answer", 42
    end)

    local k, v = f2:join()  -- join gets return values
    print(k, v)             -- prints: answer, 42

end, "Hello World!")

f1:join()
```

## Timers

Timers allow you to suspend states for periods and wake them up again
after the period has expired.

### luv.timer.create()

Constructor. Takes no arguments. Returns a timer instance.

### timer:start(delay, repeat)

Takes two parameters, `delay` and `repeat` are in milliseconds.

### timer:wait()

Suspend the currently running state and resume when the timer
fires.

### timer:stop()

Stop the timer.

### timer:again()

Stop the timer, and if it is repeating restart it using the repeat value
as the timeout

### Timer Example:

```Lua
local luv = require("luv")

local timer = luv.timer.create()
-- start after 1 second and repeat after 100ms
timer:start(1000, 100)
for i=1, 10 do
   -- the call to wait blocks here, but would schedule any
   -- ready fibers (if there were any)
   timer:wait()
   print("tick"
end
timer:stop()
```

## Filesystem operations

In general, file system operations return an integer on success
(usually 0) and `false` along with an error message on failure.

### luv.fs.open(path, mode, perm)

Open a file.

* `path` is the path of the file to open.
* `mode` is one of `w`, `w+`, `r`, `r+`, `a`, `a+`
* `perm` is a string representation of an octal number: i.e. `644`

Returns a `file` object. See below for `file` object methods.

### luv.fs.unlink(path)

Delete a file.

### luv.fs.mkdir(path)

Create a directory.

### luv.fs.rmdir(path)

Delete a directory.

### luv.fs.readdir(path)

Reads the entries of a directory. On sucess returns a table
including the entry names.

### luv.fs.stat(path)

Stats the supplied path and returns a table of key, value 
pairs.

### luv.fs.rename(path, newpath)

Renames a file or directory.

### luv.fs.sendfile(outfile, infile)

Efficiently copy data from infile to outfile.

### luv.fs.chmod(path, mode)

Change file or directory mode. The `mode` argument is a string
representation of an octal number: i.e. '644'

### luv.fs.chown(path, uid, gid)

Change the ownership of a path to the supplied `uid` and `gid`.
Both `uid` and `gid` are integers.

### luv.fs.utime(path, atime, mtime)

Change the access and modification time of a path

### luv.fs.lstat(path)

Stat a link.

### luv.fs.link(srcpath, dstpath)

Create a hard link from `srcpath` to `dstpath`

### luv.fs.symlink(srcpath, dstpath, mode)

Create a symbolic link from `srcpath` to `dstpath` with `mode` flags.
The `mode` argument takes the same values as for `luv.fs.open`.

### luv.fs.readlink(path)

Dereference a symbolic link and return the target.

### luv.fs.cwd()

Returns the current working directory of the running process.

### luv.fs.chdir(path)

Change directory to `path`.

### luv.fs.exepath()

Returns the path of the executable.
 
### file:read(len)

Attempts to read `len` number of bytes from the file and returns the number
of bytes actually read followed by the data.

### file:write(data [,offset])

Write `data` to the file. If the optional `offset` argument is given, then
write start at that offset. Otherwise write from the start of the file.

### file:close()

Close the file.

### file:stat()

Stat the file. Same return value as for `luv.fs.stat`

### file:sync()

Sync all pending data and metadata changes to disk.

### file:datasync()

Sync data to disk.

### file:utime(atime, mtime)

Like `luv.fs.utime` but uses the current file object.

### file:chmod(mode)

Like `luv.fs.chmod` but uses the current file object.

### file:chown(uid, gid)

Like `luv.fs.chown` but uses the current file object.

### file:truncate()

Truncate the file.

## TCP Streams

### luv.net.tcp()

Creates and returns a new unbound and disconnected TCP socket.

### tcp:bind(host, port)

Bind to the given `host` and `port`

### tcp:listen([backlog])

Start listening for incoming connections. If `backlog` is given then
that sets the maximum backlog for pending connections. If no `backlog`
is given, then it defaults to 128.  

### tcp:accept(tcp2)

Calls `accept` with `tcp2` becoming the client socket. Used as follows:

```Lua

local server = luv.net.tcp()
server:bind(host, port)
while true do
   local client = luv.net.tcp()
   server:accept(client)
   -- do something with the client, then close
   client:close()
end

```

### tcp:connect(host, port)

Connect to a given `host` on `port`. Note that host must be a dotted quad.
To resolve a domain name to IP address, use `getaddrinfo`

### tcp:getsockname()

Returns a table with fields `family`, `port` and `address` filled of the
current socket. Can be called on both connected and bound sockets.

### tcp:getpeername()

Returns a table with fields `family`, `port` and `address` filled of the
peer socket.  Can only be called on connected sockets.

### tcp:keepalive(enable, seconds)

Enable or disable TCP keepalive. The first arugment is a boolean. If
`true` then sets the keepalive time to the number of `seconds`. If
`false` then disables and `seconds` is ignored.

### tcp:nodelay(enable)

Enable or disable nagle's algorithm for this socket. The `enable`
argument must be a boolean.

### tcp:read()

Reads data from the socket. Returns the number of bytes read followed
by the data itself.

### tcp:readable()

Does a non-blocking check to see if the socket is readable.

### tcp:write(data)

Writes `data` to the socket.

### tcp:writable()

Does a non-blocking check to see if the socket is writable.

### tcp:shutdown()

Shutdown the socket (inform the peer that we've finished with it)

### tcp:close()

Close the socket.

### tcp:start()

Start reading from the socket. Called automatically during read.

### tcp:stop()

Stop reading from a socket. Called automatically during libuv's read
callback if there are no fibers waiting to be roused.

## Processes

See ./examples/proc.lua for now.

## Threads

See ./examples/thread.lua for now.

## ØMQ

See ./examples/zmq.lua for now.

## Serialization

Luv ships with a binary serializer which can serialize and deserialize
Lua tuples. Tuples can contain tables (with cycles), Lua functions (with
upvalues) any scalar value. Function upvalues must themselves be of a type
which can be serialized. Coroutines and C functions can *not* be serialized.

### luv.codec.encode(arg1, ..., argN)

Serializes tuple `arg1` through `argN` and returns a string which can
be passed to `luv.codec.decode`.

### luv.codec.decode(string)

Deserializes `string` previously serialized with a call to `luv.codec.encode`

Returns the decoded tuple.

### Serliazation hook

For userdata and tables, a special hook is provided. If the metatable
has a `__codec` method defined, then that is called. The `__codec`
hook is called with the object as argument and is expected to return
two values.

The first return value may be either a serializable function or a
string. The second value may be any serializable value.

If the first return value is a function, then it is called during
deserialization with the second return value as parameter.

If the first return value is a string, then the deserializer looks
for a function keyed on that string inside Lua's registry table,
and then in the global table, in that order. If no function is
found, then an error is raised. If a function is found, then this
function is called with the second return value is argument.

For example:

```Lua
local luv = require("luv")

module("my.module", package.seeall)

Point = { } 
Point.__index = Point
Point.new = function()
   return setmetatable({ }, Point)
end
Point.move = function(self, x, y)
   self.x = x 
   self.y = y 
end
Point.__codec = function(self)
   return function(spec)
      local Point = require('my.module').Point
      return setmetatable(spec, Point)
   end, { x = self.x, y = self.y }
end


module("main", package.seeall)

local Point = require("my.module").Point
local obj = Point.new()
obj:move(1, 2)

local str = luv.codec.encode(obj)
local dec = luv.codec.decode(str)

assert(dec.x == 1 and dec.y == 2)
assert(type(dec.move) == 'function')
```

# ACKNOWLEDGEMENTS

* Tim Caswell (creationix) and the Luvit authors
* Vladimir Dronnikov (dvv)

# LICENSE

   Parts Copyright The Luvit Authors

   Copyright 2012 Richard Hundt

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.

# TODO

* uv_poll_t wrapper
* test UDP stuff
* ØMQ devices and utils
* finish docs


