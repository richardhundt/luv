local luv = require("luv")
local reg = debug.getregistry()
assert(reg["luv:lib:decoder"])
assert(type(reg["luv:lib:decoder"]) == "function")

local str = luv.codec.encode(luv)
print(string.format('%q', str))

local lib = luv.codec.decode(str)
print(lib)
assert(lib == luv)

local tup = { answer = 42 }
local fun = function()
   return tup
end

local str = luv.codec.encode(fun)
print(string.format('%q', str))
local fun = luv.codec.decode(str)
print(fun().answer)

local obj = { answer = 42 }
setmetatable(obj, {
   __codec = function(o)
      print("__codec called with:", o)
      local u = { "upval" }
      return function(v)
         print("decoder called with:", v, "upvalue is ", u[1])
         return { answer = v }
      end, 42
   end
})

local str = luv.codec.encode(obj)
print(string.format('%q', str))
local obj = luv.codec.decode(str)
assert(obj.answer == 42)

local obj = { }
obj.a = { }
obj.a.b = { }
obj.a.b.c = obj.a.b
local str = luv.codec.encode(obj)
local dec = luv.codec.decode(str)
assert(obj.a)
assert(obj.a.b)
assert(obj.a.b.c == obj.a.b)


