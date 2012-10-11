local ray = require("ray")

local work = function(a)
   for i=1, 10000000 do --> that's a big number, so use LuaJIT-2 :)
      coroutine.yield()
   end
end

local f1 = ray.fiber.create(work, "a")
local f2 = ray.fiber.create(work, "b")

f1:ready()
f2:ready()

f1:join()
f2:join()


