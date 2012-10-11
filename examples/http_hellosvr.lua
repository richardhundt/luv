local luv = require('luv')

local http_parser = require('http.parser')

local function p(x)
  for k, v in pairs(x) do
    print(k, v)
  end
end

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
           --local state = luv.self()
            local request = { }
            -- parse http
            local parser
            parser = http_parser.request({
               on_message_begin = function()
                  print('MSGBEGIN')
                  request = { }
                  request.client = client
                  request.headers = { }
                  request.emit = function(self, event, ...)
                    print('EMIT', self, event, ...)
                  end
               end,
               on_url = function(url)
                  print('URL', url)
                  request.url = url
                  -- TODO: parse url
               end,
               on_header = function(hkey, hval)
                  --print('HEADER', hkey, hval)
                  request.headers[hkey:lower()] = hval
               end,
               on_headers_complete = function()
                  print('HDRDONE')
                  request.method = parser:method()
                  request.upgrade = parser:is_upgrade()
                  request.should_keep_alive = parser:should_keep_alive()
               end,
               on_body = function(chunk)
                  print('BODY', chunk)
                  if chunk ~= nil then
                     request:emit("data", chunk)
                  else
                     --state:ready()
                     print('MSGEND!')
                     --p(request)
                     request:emit("end")
            -- attempt at introducing delays
            if true then
               local delay = luv.timer.create()
               delay:start(10, 0)
               delay:wait()
               delay:stop()
            end
            local response = ("Hello\n") --:rep(1000)
            local rc
            if request.should_keep_alive then
              rc = client:write("HTTP/1.1 200 OK\r\nConnection: keep-alive\r\nContent-Length: " .. #response .. "\r\n\r\n" .. response)
            else
              rc = client:write("HTTP/1.0 200 OK\r\nConnection: close\r\nContent-Length: " .. #response .. "\r\n\r\n" .. response)
            end
            if rc < 0 then
               print("WRITE ERR:", rc)
            end
                  end
               end,
               on_message_complete = function()
                  print('MSGDONE')
                  --if parser.should_keep_alive() then
                    parser:reset()
                  --end
               end
            })
      while true do
         --print("READ LOOP TOP")
         local nread, chunk = client:read()
         --print("READ:", nread, chunk)
         if nread then
            local nparsed = parser:execute(chunk)
            if nparsed < nread then
               if request.upgrade then
                  request:emit("data", chunk:sub(nparsed + 1))
               else
                  request:emit("error", "parse error")
               end
            end
         else
            --print("CLOSING")
            request:emit("error", "short read")
            client:close()
            break
         end
      end
   end)
   fib:ready()
end
