local luv = require('luv')

luv.stdout:write("Hello World!")
local mesg = luv.stdin:read()
luv.stderr:write("thanks, you said:"..mesg)


