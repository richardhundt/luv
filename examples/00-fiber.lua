local fiber = require("ray.fiber")
print(fiber)

local f0 = fiber.create(function()
   for i=1, 1000000 do
      --print("A - tick:", i)
      coroutine.yield()
   end
end)
local f1 = fiber.create(function()
   for i=1, 1000000 do
      --print("B - tick:", i)
      coroutine.yield()
   end
end)

f0:ready()
print("CALLED READY")
f1:join()
print("HERE")

