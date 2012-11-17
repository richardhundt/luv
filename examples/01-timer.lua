local timer = require("ray.timer")
local fiber = require("ray.fiber")
print(timer)

local t1 = timer.create()
t1:start(0, 500)
print("STARTED")

local f1 = fiber.create(function()
   print("enter A")
   for i=1, 10 do
      print(t1:wait())
      print("A - tick...", i)
   end
end)
---[[
local f2 = fiber.create(function()
   print("enter B")
   for i=1, 10 do
      print(t1:wait())
      print("B - tick...", i)
   end
end)
--]]
print("FIBER:", f1)
f1:ready()
f2:join()
t1:stop()
