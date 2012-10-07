local luv = require("luv")

local f = luv.fiber.create(function()
   local fh = luv.fs.open("/tmp/foo", "w+",'644')
   fh:write("Hey Globe!")
   fh:close()
   local p = luv.process.spawn("/bin/cat", { "/tmp/foo", stdout = luv.stdout })
   print("SPAWNED:", p)
end)
f:ready()
f:join()

