package = "luv"
version = "scm-1"
source = {
  url = "git://github.com/richardhundt/luv",
}
description = {
  summary  = "Thermonuclear battery pack for Lua",
  detailed = "Luv is an attempt to do libuv bindings to Lua in a style more suited to a language with coroutines than edge-triggered event-loop style programming with callbacks",
  homepage = "https://github.com/richardhundt/luv",
  license  = "http://www.apache.org/licenses/LICENSE-2.0",
}
dependencies = {
  "lua >= 5.1"
}
build = {
  type = "command",
  build_command = "cmake -E make_directory build && cd build && cmake -D INSTALL_CMOD=$(LIBDIR) .. && $(MAKE)",
  install_command = "cd build && $(MAKE) install",
}
