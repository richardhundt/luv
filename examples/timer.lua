local luv = require('luv')

local f1 = luv.fiber.create(function()
   print("ENTER")
   local t1 = luv.timer.create()
   t1:start(1000, 100)
   for i=1, 10 do
      print("tick:", i)
      print(t1:wait())
   end
   t1:stop()
end)

f1:join()

