local luv = require('luv')

local server = luv.net.tcp()

print("SERVER:", server)
print("BIND:", server:bind("0.0.0.0", 8080))
print("LISTEN:", server:listen(32768))

while true do
   --print("ACCEPT LOOP TOP")

   local client = luv.net.tcp()
   local rc = server:accept(client) -- block here
   if not rc then
     print("ACCEPT ERR:", rc)
   end

   local fib = luv.fiber.create(function()
      --print("CHILD")
      while true do
         --print("READ LOOP TOP")
         local got, str = client:read()
         --print("READ:", got, str)
         if got then
            -- attempt at introducing delays
            if false then
               local delay = luv.timer.create()
               delay:start(100, 0)
               delay:wait()
               delay:stop()
            end
            local rc = client:write("HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 6\r\n\r\nHello\n")
            -- TODO: needa normalize return values to such from :read()
            if rc < 0 then
               print("WRITE ERR:", rc)
            end
         -- read error means peer reset connection.
         -- no need to close descriptor, it must be closed already
         elseif got == false then
            print("RESET")
            break
         -- read EOF, just close the client
         else
            --print("CLOSING")
            client:close()
            break
         end
      end
   end)
   fib:ready()
end
