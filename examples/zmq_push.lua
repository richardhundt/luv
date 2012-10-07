local luv = require('luv')

local zmq = luv.zmq.create(2)

local pub = zmq:socket(luv.zmq.PUSH)
pub:bind('tcp://127.0.0.1:1234')

local prod = luv.fiber.create(function()
   print("enter prod:")
   for i=1, 10 do
      pub:send("tick: "..i)
   end
   pub:close()
end)

prod:ready()
prod:join()

