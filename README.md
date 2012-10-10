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
* [libuv] idle watchers
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
(20,000,000 context switches / second on my laptop).

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
A state can be either a thread - including the main thread - or a fiber.

States are not resumed immediately when ready, but are fair queued
to be resumed at the next possible time, after they are signalled
as being `ready`, typically by a libuv callback.

The semantics of suspending a state depends on whether it is a thread
or a fiber.

* Suspending a fiber is equivalent to removing it from the scheduing queue.
* Suspending a thread is equivalent to running the event loop and scheduler.
* Readying a fiber is equivalent to inserting it into the scheduling queue.
* Readying a thread is equivalent to interrupting the event loop.

Any objects (timers, tcp, idle, etc.) may also run from the main thread
while not blocking active fibers.

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
   print("tick")
end
timer:stop()
```

## Idle watchers

Idle watchers run when there's nothing else to do. The object will rouse
the waiting fibers repeatedly.

### luv.idle.create()

Create an idle watcher.

### idle:start()

Start the idle watcher.

### idle:stop()

Stop the idle watcher.

### idle:wait()

Suspend the current state until there's nothing else to do.

### Idle Example

This is from the examples. During the timer pauses, the idle watcher
unblocks the call to `idle:wait()`.

```Lua
local luv = require('luv')

local idle = luv.idle.create()
idle:start()

local idle_count = 0
local f1 = luv.fiber.create(function()
   while true do
      idle:wait()
      idle_count = idle_count + 1
   end
end)

f1:ready()

local timer = luv.timer.create()
timer:start(10, 10)

local f2 = luv.fiber.create(function()
   for i=1, 10 do
      print("TIMER NEXT:", timer:wait())
   end
   timer:stop()
end)

f2:join()
idle:stop()
print("IDLE COUNT:", idle_count)

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
 
### file:read(len[, offset])

Attempts to read `len` number of bytes from the file and returns the number
of bytes actually read followed by the data. If the optional `offset` is
given, then start reading there.

### file:write(data[, offset])

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

### tcp:read([length])

Reads data from the socket. Returns the number of bytes read followed
by the data itself. If the optional `length` argument is provided then
that is the size, in bytes, of the buffer used internally. Defaults
to the value of `LUV_BUF_SIZE` defined in luv.h (4096, currently).

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

## Pipes

TODO : add docs - their use is similar to TCP streams

### luv.pipe.create()

### pipe:open()

### pipe:bind()

### pipe:connect()

### pipe:listen()

### pipe:accept()

### pipe:read()

### pipe:write(data)

### pipe:close()

## Threads

Threads are real OS threads and run concurrently (so no global locks)
in distinct global Lua states. This means that sharing data between
threads should be done with ØMQ sockets. However, functions passed
to `luv.thread.spawn` are ordinary Lua functions and may contain
upvalues.

These upvalues are serialized as best as possible automatically
and deserialized during thread entry. The same rules apply as for
`luv.codec.encode` (see below).

Return values passed back via `thread:join()` pass through the same
serialize/deserialize process, with the same caveats. So bear in
mind that there's no true shared address space when using threads.
This is A Good Thing (tm), I'm told.

Some of Luv's own objects and library tables are handled transparently.

In particular ØMQ context objects can be passed to threads or referenced
as upvalues. ØMQ sockets and other libuv objects cannot.

Each thread has it's own libuv event loop, with the main thread running
libuv's default loop. Threads may spawn other threads as well as fibers.

### luv.thread.spawn(func, arg1, ..., argN)

Spawn a thread, using the Lua function `func` as the entry,
and serialize the rest of the arguments and pass them deserialized
back to `func` inside the new thread's global state.

Threads are spawned immediately during a call to `luv.thread.spawn`, so
they differ to fibers in that there's no call to `ready` them first.

Returns a thread object.

### thread:join()

Wait for the thread to finish. Returns the values returned by the thread
if any.

Threads may join on threads or fibers. I have no idea what happens if
a fiber joins on a thread. Bad Things probably. Haven't tried it yet.

## Utilities

### luv.self()

Returns the currently running state, which can be either a thread or a fiber.

### luv.stdin, luv.stdout and luv.stderr

Fiber friendly stream versions of the standard file descriptors

### luv.sleep(seconds)

Fiber friendly version of sleep(). The `seconds` argument may be fractional
with millisecond resolution.

### luv.hrtime()

Returns the current high-resolution time expressed in nanoseconds since
some arbitrary time in the past. May not have nanosecond resolution though.

### luv.mem_total()

Returns the total memory in bytes.

### luv.mem_free()

Returns the free memory in bytes.

### luv.cpu_info()

Returns a table containing an entry for each logical cpu. The entries
have the following fields:

* model - string containing the model name
* speed - number in mhz
* times - table with the following fields:
  * user
  * nice
  * sys
  * idle
  * irq

### luv.interface_addresses()

Returns a table containing an entry for each interface address. The
entries have the following fields:

* name - string
* is_internal - boolean
* address - string (ip4 or ip6 address)

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

### Serialization hook

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

## ØMQ

Luv provides bindings to ØMQ. The primary motivation for all this is that
I really wanted threads. And ØMQ and threads fit together like a fist in
the eye socket. ØMQ is tied into libuv's polling mechanism and has the same
suspend/resume states behaviour as other I/O watchers.

You can, of course, use ØMQ from fibers as well.

### luv.zmq.create(nthreads)

Creates a new ØMQ context object. Context objects can be shared across
different threads and may be referenced as upvalues, or passed as
arguments and return values (they survive serialization).

The `nthreads` argument controls the number of worker threads spawned
by ØMQ's internals, and defaults to `1`.

### zmq:socket(type)

Called on the ØMQ context object to create a socket of type `type`. Socket
types are described by constants defined in the `luv.zmq` table and map
to the standard ØMQ socket types with prefix removed. They are:

* REQ
* REP
* DEALER
* ROUTER
* PUB
* SUB
* PUSH
* PULL
* PAIR

The ØMQ docs explain what they all mean: http://zguide.zeromq.org/

The socket returned may _not_ be shared between threads.

### socket:bind(addr)

Bind this ØMQ socket to the address provided by `addr`. The `addr`
string is the same as documented by ØMQ (i.e. "tcp://127.0.0.1:8080", etc.)

### socket:connect(addr)

Connect this ØMQ socket to the address provided by `addr`. The `addr`
string is the same as documented by ØMQ (i.e. "tcp://127.0.0.1:8080", etc.)

### socket:send(mesg)

Send a message on the ØMQ socket.

### socket:recv()

Receive a message from the ØMQ socket.

### socket:close()

Close the ØMQ socket.

### socket:getsockopt(opt)

Get a socket option named `opt`. Note that `opt` is a string, not a numeric
constant as with the socket constructor. I'm still undecided which is better.

See the ØMQ docs.

### socket:setsockopt(opt, val)

Set a socket option named `opt`. Note that `opt` is a string, not a numeric
constant as with the socket constructor. I'm still undecided which is better.

See the ØMQ docs.

### ØMQ Example

```Lua
local zmq = luv.zmq.create(1)
local prod = luv.thread.create(function()
   local pub = zmq:socket(luv.zmq.PAIR)
   pub:bind('inproc://#1')

   print("enter prod:")
   for i=1, 10 do
      pub:send("tick: "..i)
   end
   pub:send("STOP")
   assert(pub:recv() == "OK")
   pub:close()
end)

local cons = luv.thread.create(function()
   local sub = zmq:socket(luv.zmq.PAIR)
   sub:connect('inproc://#1')

   print("enter cons")
   while true do
      local msg = sub:recv()
      if msg == "STOP" then
         sub:send("OK")
         break
      end
   end
   sub:close()
end)

cons:join()
```


# ACKNOWLEDGEMENTS

* Tim Caswell (creationix) and the Luvit authors
* Aleksandar Kordic
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
* unify ØMQ constants (either strings or numbers)
* finish docs


