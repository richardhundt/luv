# NAME

Luv - Lua + libuv + ØMQ

# SYNOPSIS

# FEATURES

* scheduled fibers
* [libuv] TCP sockets
* [libuv] timers
* [libuv] filesystem operations
* [libuv] OS threads
* ØMQ 3.x for the rest 

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

Here's an example of a timer:

```Lua
local luv = require('luv')

local t1 = luv.timer.create()
t1:start(1000, 200)

for i=1, 10 do
  -- suspend here until the timer triggers
  t1:next()
  print("A - next - ", i)
end
```

Of course, we haven't created any fibers, so the whole process
is block at the call to `t1:next()`, which is what you expect
since there's nothing else to scheduler.

Here's the canonical TCP echo server as a slightly less trivial
example:

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

main:ready()
main:join()
```

A key point is that fibers run at highest priority, so that a process
under I/O load gets doesn't starve the tasks which actually process
the data.

Once all pending fibers have been given a chance to run, the
event loop kicks in and polls for events and the wakes up
any suspended fibers waiting on events.
 
## ØMQ + Fibers

```Lua
local zmq = luv.zmq.create(1)

local pub = zmq:socket(luv.zmq.PUSH)
pub:bind('inproc://127.0.0.1:1234')

local prod = luv.fiber.create(function()
   for i=1, 10 do
      pub:send("tick: "..i)
   end
end)

local cons = luv.fiber.create(function()
   local sub = zmq:socket(luv.zmq.PULL)
   sub:connect('inproc://127.0.0.1:1234')

   for i=1, 1000000 do
      local msg = sub:recv()
      print("GOT: "..msg)
   end

   sub:close()
end)

prod:ready()
cons:ready()

cons:join()
```

## ØMQ + Threads

TODO - but just a note that the function passed to `luv.thread.create`
is serialize first. Luv tries to be smart about upvalues referencing
objects it knows about, so this looks deceptive. In general you can
only reference scalar upvalues, the ØMQ context object, and the `luv`
library table itself. Same goes for additional arguments passed to
`luv.thread.create`.

When I have a bit more time, I'd like to experiment with detecting
references to tables and other userdata and create proxy objects
in the child threads which do RPC to objects in the outer scope
via ØMQ inproc sockets. And provide decent serialization for
the cases where cloning objects is what you need, of course.


```Lua
local luv = require('luv')

local zmq = luv.zmq.create(0)
local chan = zmq:socket(luv.zmq.PUSH)
chan:bind('inproc://127.0.0.1:5556')

local function work()
   local chan = zmq:socket(luv.zmq.PULL)
   chan:connect('inproc://127.0.0.1:5556')
   for i=1, 10 do
      print(chan:recv())
   end 
   chan:close()
end
local t1 = luv.thread.create(work)
for i=1, 10 do
   chan:send("do: "..i)
end
chan:close()
t1:join()
```

More to come.
