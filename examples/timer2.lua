local luv = require("luv")

local t1 = luv.timer.create()
t1:start(1000, 200)

local worker1 = function()
   for i=1, 10 do
      t1:wait()
      print("A", "tick:", i)
   end
end

print("sleep half second...")
luv.sleep(0.5)
print("ok")

local worker2 = function()
   for i=1, 10 do
      t1:wait()
      print("B", "tick:", i)
   end
end

local f1 = luv.fiber.create(worker1)
local f2 = luv.fiber.create(worker2)

f1:ready()
f2:ready()

f1:join()
f2:join()

t1:stop()

print("DONE")

