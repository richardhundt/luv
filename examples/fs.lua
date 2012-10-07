local luv = require("luv")

local t = luv.fs.stat("/tmp")
for k,v in pairs(t) do
   print(k, "=>", v)
end

local f1 = luv.fiber.create(function()
   print("enter")
   local file = luv.fs.open("/tmp/cheese.ric", "w+", "664")
   print("file:", file)
   print("write:", file:write("Hey Bro!"))
   print("close:", file:close())
end)

f1:ready()
f1:join()

local file = luv.fs.open("/tmp/cheese.ric", "r", "664")
print("READ:", file:read())
file:close()
print("DELETE:", luv.fs.unlink("/tmp/cheese.ric"))

