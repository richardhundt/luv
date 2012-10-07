local luv  = require("luv")
local host = luv.net.getaddrinfo("www.google.com")
local sock = luv.net.tcp()
print("conn:", sock:connect(host, 80))
print("write:", sock:write("GET / HTTP/1.0\r\nHost: www.google.com\r\n\r\n"))
print("read:", sock:read())

