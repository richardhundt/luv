# CMake build script for ZeroMQ on Windows

cmake_minimum_required (VERSION 2.8)
project (zmq)

option (WITH_OPENPGM "Build with support for OpenPGM" OFF)

# WARNING: Windows Python will override Cygwin yet not work with Asciidoc.
#find_package (PythonInterp REQUIRED)
# Workaround, manually set Python location
set(PYTHON_EXECUTABLE c:/cygwin/bin/python2.6.exe CACHE FILEPATH "Python interpreter executable")
# TODO: Replace with FindAsciidoc.cmake
set(ASCIIDOC_EXECUTABLE c:/cygwin/bin/asciidoc CACHE FILEPATH "AsciiDoc executable")

set(OPENPGM_ROOT /libpgm/libpgm-5.1.118-1~dfsg/openpgm/pgm CACHE PATH "Location of OpenPGM")

mark_as_advanced(PYTHON_EXECUTABLE ASCIIDOC_EXECUTABLE)

#-----------------------------------------------------------------------------
# force off-tree build

if(${PROJECT_SOURCE_DIR} STREQUAL ${PROJECT_BINARY_DIR})
message(FATAL_ERROR "CMake generation is not allowed within the source directory!
Remove the CMakeCache.txt file and try again from another folder, e.g.:

   del CMakeCache.txt
   mkdir cmake-make
   cd cmake-make
   cmake ..
")
endif(${PROJECT_SOURCE_DIR} STREQUAL ${PROJECT_BINARY_DIR})

#-----------------------------------------------------------------------------
# default to Release build

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING
      "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
      FORCE)
endif(NOT CMAKE_BUILD_TYPE)

set(LIBRARY_OUTPUT_PATH  ${PROJECT_BINARY_DIR})

#-----------------------------------------------------------------------------
# platform specifics

if (WIN32)
	add_definitions(
          -DWIN32
		-DDLL_EXPORT
# NB: May require tweaking for highly connected applications.
		-DFD_SETSIZE=1024
		-D_CRT_SECURE_NO_WARNINGS
	)

# Parallel make.
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")

# Optimization flags.
# http://msdn.microsoft.com/en-us/magazine/cc301698.aspx
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /GL")
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG")
set(CMAKE_MODULE_LINKER_FLAGS_RELEASE "${CMAKE_MODULE_LINKER_FLAGS_RELEASE} /LTCG")

endif (WIN32)

#-----------------------------------------------------------------------------
# source files

set(cxx-sources
	address.cpp
	clock.cpp
	ctx.cpp
	dealer.cpp
	decoder.cpp
	devpoll.cpp
	dist.cpp
	encoder.cpp
	epoll.cpp
	err.cpp
	fq.cpp
	io_object.cpp
	io_thread.cpp
	ip.cpp
	ipc_address.cpp
	ipc_connecter.cpp
	ipc_listener.cpp
	kqueue.cpp
	lb.cpp
	mailbox.cpp
	msg.cpp
	mtrie.cpp
	object.cpp
	options.cpp
	own.cpp
	pair.cpp
	pgm_receiver.cpp
	pgm_sender.cpp
	pgm_socket.cpp
	pipe.cpp
	poll.cpp
	poller_base.cpp
	precompiled.cpp
	proxy.cpp
	pub.cpp
	pull.cpp
	push.cpp
	random.cpp
	reaper.cpp
	rep.cpp
	req.cpp
	router.cpp
	select.cpp
	session_base.cpp
	signaler.cpp
	socket_base.cpp
	stream_engine.cpp
	sub.cpp
	tcp.cpp
	tcp_address.cpp
	tcp_connecter.cpp
	tcp_listener.cpp
	thread.cpp
	trie.cpp
	v1_decoder.cpp
	v1_encoder.cpp
	xpub.cpp
	xsub.cpp
	zmq.cpp
	zmq_utils.cpp
)

set(rc-sources
	version.rc
)

include_directories(include ${PROJECT_BINARY_DIR})

#-----------------------------------------------------------------------------
# optional modules

if(WITH_OPENPGM)
	add_definitions(
		-DZMQ_HAVE_OPENPGM
	)

	include_directories(
		${OPENPGM_ROOT}/include
	)
	if (CMAKE_SIZEOF_VOID_P EQUAL 8)
# Win64
		if (CMAKE_BUILD_TYPE STREQUAL "Debug")
			set(OPENPGM_LIBRARYDIR ${OPENPGM_ROOT}/debug64/lib)
		else (CMAKE_BUILD_TYPE STREQUAL "Debug")
			set(OPENPGM_LIBRARYDIR ${OPENPGM_ROOT}/build64/lib)
		endif (CMAKE_BUILD_TYPE STREQUAL "Debug")
	else (CMAKE_SIZEOF_VOID_P EQUAL 8)
# Win32
		if (CMAKE_BUILD_TYPE STREQUAL "Debug")
			set(OPENPGM_LIBRARYDIR ${OPENPGM_ROOT}/debug/lib)
		else (CMAKE_BUILD_TYPE STREQUAL "Debug")
			set(OPENPGM_LIBRARYDIR ${OPENPGM_ROOT}/build/lib)
		endif (CMAKE_BUILD_TYPE STREQUAL "Debug")
	endif (CMAKE_SIZEOF_VOID_P EQUAL 8)
	link_directories(
		${OPENPGM_LIBRARYDIR}
	)
endif(WITH_OPENPGM)

#-----------------------------------------------------------------------------
# source generators

foreach (source ${cxx-sources})
	list(APPEND sources ${PROJECT_SOURCE_DIR}/src/${source})
endforeach()

foreach (source ${rc-sources})
	list(APPEND sources ${PROJECT_BINARY_DIR}/${source})
	configure_file(${PROJECT_SOURCE_DIR}/src/${source}.in ${PROJECT_BINARY_DIR}/${source})
endforeach()

if (WIN32)
add_custom_command(
	OUTPUT ${PROJECT_BINARY_DIR}/platform.hpp
	COMMAND ${CMAKE_COMMAND}
	ARGS	-E
		copy
		${PROJECT_SOURCE_DIR}/builds/msvc/platform.hpp
		${PROJECT_BINARY_DIR}/platform.hpp
	DEPENDS ${PROJECT_SOURCE_DIR}/builds/msvc/platform.hpp
)
else ()
	file(WRITE "${PROJECT_BINARY_DIR}/platform.hpp" "#define ZMQ_HAVE_LINUX\n#define ZMQ_HAVE_UIO 1\n")
endif (WIN32)
list(APPEND sources ${PROJECT_BINARY_DIR}/platform.hpp)

if (CMAKE_SIZEOF_VOID_P EQUAL 8)
	set (nsis-template ${PROJECT_SOURCE_DIR}/cmake/NSIS.template64.in)
else (CMAKE_SIZEOF_VOID_P EQUAL 8)
	set (nsis-template ${PROJECT_SOURCE_DIR}/cmake/NSIS.template32.in)
endif (CMAKE_SIZEOF_VOID_P EQUAL 8)
add_custom_command(
	OUTPUT ${PROJECT_BINARY_DIR}/NSIS.template.in
	COMMAND ${CMAKE_COMMAND}
	ARGS    -E
		copy
		${nsis-template}
		${PROJECT_BINARY_DIR}/NSIS.template.in
	DEPENDS ${nsis-template}
)

#-----------------------------------------------------------------------------
# output


if (UNIX)
	add_library(zmq STATIC ${sources} ${PROJECT_BINARY_DIR}/NSIS.template.in)
	find_package(Threads REQUIRED)
	target_link_libraries(zmq stdc++ ${CMAKE_THREAD_LIBS_INIT} rt)
	#set_target_properties(libzmq PROPERTIES PREFIX "")
else ()
	add_library(libzmq SHARED ${sources} ${PROJECT_BINARY_DIR}/NSIS.template.in)
	target_link_libraries(libzmq ws2_32.lib rpcrt4.lib)
	set_target_properties(libzmq PROPERTIES RELEASE_POSTFIX "${_zmq_COMPILER}-mt" DEBUG_POSTFIX "${_zmq_COMPILER}-mt-gd")
endif (UNIX)

include (InstallRequiredSystemLibraries)

# end of file
