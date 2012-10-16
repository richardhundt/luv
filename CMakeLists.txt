cmake_minimum_required(VERSION 2.8)

project(luv)

# general info
set(VERSION_MAJOR 0)
set(VERSION_MINOR 1)

# configurable options
set(USE_ZMQ OFF)
set(USE_HTTP OFF)
set(USE_STRICT ON)

option(USE_ZMQ "Include zmq" ${USE_ZMQ})
option(USE_HTTP "Include http" ${USE_HTTP})
option(USE_STRICT "Treat warning as errors" ${USE_STRICT})
## TODO: include config.h into luv.h
#configure_file(cmake/config.h.in config.h)

# setup compiler flags
add_definitions(-fPIC)
if(USE_STRICT)
  add_definitions(-Werror)
endif()

# collect source files
list(APPEND SOURCES
  src/luv.c src/luv_cond.c src/luv_state.c src/luv_fiber.c
  src/luv_thread.c src/luv_codec.c src/luv_object.c
  src/luv_timer.c src/luv_idle.c src/luv_fs.c src/luv_stream.c
  src/luv_pipe.c src/luv_net.c src/luv_process.c
)

# find lua/luajit
include(cmake/FindLua.cmake)
include_directories(${LUA_INCLUDE_DIR})
list(APPEND LIBS ${LUA_LIBRARIES})

# setup linker flags
find_package(Threads REQUIRED)
list(APPEND LIBS ${CMAKE_THREAD_LIBS_INIT})

# build libuv
execute_process(WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  COMMAND git submodule update --init src/uv)
file(COPY "${PROJECT_SOURCE_DIR}/cmake/uv/CMakeLists.txt"
  DESTINATION "${PROJECT_SOURCE_DIR}/src/uv")
add_subdirectory("${PROJECT_SOURCE_DIR}/src/uv")
list(APPEND LIBS uv rt)

# build libzmq
if(USE_ZMQ)
  add_definitions(-DUSE_ZMQ)
  list(APPEND SOURCES src/luv_zmq.c)
  execute_process(WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    COMMAND git submodule update --init src/zmq)
  file(COPY "${PROJECT_SOURCE_DIR}/cmake/zmq/CMakeLists.txt"
    DESTINATION "${PROJECT_SOURCE_DIR}/src/zmq")
  add_subdirectory("${PROJECT_SOURCE_DIR}/src/zmq")
  list(APPEND LIBS zmq stdc++)
endif(USE_ZMQ)

# build target module luv.so
add_library(luv SHARED ${SOURCES})
target_link_libraries(luv ${LIBS})
set_target_properties(luv PROPERTIES PREFIX "")

# install
if(INSTALL_CMOD)
  install(TARGETS luv LIBRARY DESTINATION "${INSTALL_CMOD}")
else()
  message("WARNING: " "No install destination is given. Manually copy luv.so")
endif()
