local luv = require('luv')

local server = luv.net.tcp()

print("SERVER:", server)
print("BIND:", server:bind("127.0.0.1", 8080))
print("LISTEN:", server:listen())

while true do
   print("ACCEPT LOOP TOP")

   local client = luv.net.tcp()
   print("ACCEPT:", server:accept(client)) -- block here

   local fib = luv.fiber.create(function()
      print("CHILD")
      while true do
         print("READ LOOP TOP")
         local got, str = client:read(1024)
         print("READ:", got, str)
         if got then
            client:write("you said: "..str)
            print("WRITE OK")
         else
            print("CLOSING")
            client:close()
            break
         end
      end
   end)
   fib:ready()
end

