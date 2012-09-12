# NAME

luv - luvit minus the 'it'

# DESCRIPTION

The libuv bindings shamelessly stolen from the ![luvit](https://github.com/luvit/luvit) code base. So you get just the libuv core Lua bindings. No dependency on LuaJIT.

The plan is to synthesise ![libuv-lua](https://github.com/richardhundt/libuv-lua) and this library into something reasonably complete.

The bindings dispatch to handlers registered on the function environment of the relevant userdata stream objects, so you need to set that up yourself for now.

Here's a simple TCP echo server:

```Lua
local uv = require('luv')
local t  = uv.newTcp()
uv.tcpBind(t, '127.0.0.1', 8080)
uv.listen(t, function(...)
   local c = uv.newTcp()
   uv.accept(t, c)
   -- set the handler on the stream
   local o = debug.getfenv(c)
   o['data'] = function(data, len)
      count = count + 1
      uv.write(c, "you said:"..data)
   end
   uv.readStart(c)
end)
uv.run()
```

