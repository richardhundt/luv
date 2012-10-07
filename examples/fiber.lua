local luv = require('luv')
assert(luv)

local boss = luv.fiber.create(function()

   print("boss enter")

   local work = function(a, b)
      print("work:", a, b)
      for i=1, 10 do
         print(a, "tick:", i, a)
         luv.fiber.yield()
      end
      return b, "cheese"
   end

   local f1 = luv.fiber.create(work, "a1", "b1")
   local f2 = luv.fiber.create(work, "a2", "b2")

   f1:ready()
   f2:ready()

   print("join f1:", f1:join())
   print("join f2:", f2:join())

   return 1,2,3
end)
print("BOSS: ", boss)
--boss:ready()

print("join boss:", boss:join())

