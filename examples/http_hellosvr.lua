local luv = require('luv')

local server = luv.net.tcp()

print("SERVER:", server)
print("BIND:", server:bind("0.0.0.0", 8080))
print("LISTEN:", server:listen())

while true do
   --print("ACCEPT LOOP TOP")

   local client = luv.net.tcp()
   server:accept(client) -- block here
   --print("ACCEPT:", server:accept(client)) -- block here

   local fib = luv.fiber.create(function()
      --print("CHILD")
      while true do
         --print("READ LOOP TOP")
         local got, str = client:read(1024)
         --print("READ:", got, str)
         if got then
            client:write("HTTP/1.1 200 OK\r\nContent-Length: 6\r\n\r\nHello\n")
            --print("WRITE OK")
            client:close()
            break
         else
            --print("CLOSING")
            client:close()
            break
         end
      end
   end)
   fib:ready()
end

