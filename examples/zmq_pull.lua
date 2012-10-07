local luv = require('luv')

local zmq = luv.zmq.create(2)

local cons = luv.fiber.create(function()
   local sub = zmq:socket(luv.zmq.PULL)
   sub:connect('tcp://127.0.0.1:1234')

   print("enter cons")
   for i=1, 10 do
      local msg = sub:recv()
      print("GOT: "..msg)
   end

   sub:close()
end)

cons:ready()
cons:join()

