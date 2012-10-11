package = 'luv'
version = 'scm-1'
source = {
  url = 'git://github.com/richardhundt/luv.git'
}
description = {
  summary  = "libuv bindings for Lua",
  detailed = 'Luv is an attempt to do libuv bindings to Lua in a style more suited to a language with coroutines than edge-triggered event-loop style programming with callbacks',
  homepage = 'https://github.com/richardhundt/luv',
  license  = 'http://www.apache.org/licenses/LICENSE-2.0',
}
dependencies = {
  'lua >= 5.1'
}
build = {
  type = 'builtin',
  modules = {
    luv = {
      defines = {
        "USE_ZMQ=1",
      },
      sources = {
        "src/luv.c",
        "src/luv_cond.c",
        "src/luv_state.c",
        "src/luv_fiber.c",
        "src/luv_thread.c",
        "src/luv_codec.c",
        "src/luv_object.c",
        "src/luv_timer.c",
        "src/luv_idle.c",
        "src/luv_fs.c",
        "src/luv_stream.c",
        "src/luv_pipe.c",
        "src/luv_net.c",
        "src/luv_process.c",
        "src/luv_zmq.c",
      },
      incdirs = {
        "src/",
        "src/uv/include",
        "src/zmq/include",
      },
      libdirs = {
        "src/uv",
        "src/zmq/src/.libs",
      },
      libraries = {
        "uv",
        "zmq",
        "stdc++",
        "rt",
      }
    }
  }
}
