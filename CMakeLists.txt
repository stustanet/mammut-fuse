cmake_minimum_required(VERSION 3.0)

project (mammutfs)

set (CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
message (${CMAKE_MODULE_PATH})

set(SAVE_FILE_HANDLES YES CACHE BOOL
	"limit the amount of file handles used, might cause problems with NFS")
set(ENABLE_TRACELOG YES CACHE BOOL
	"Enable verbose trace logging of every syscall")

set(CONFIG_DEBUG NO CACHE BOOL
	"Enable verbose debugging of config values")

set(TRACE_GETATTR NO CACHE BOOL
	"Enable also getattr tracelog (very noisy)")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
	${CMAKE_CURRENT_BINARY_DIR}/config.h)

set (SOURCES
	src/communicator.cpp
	src/main.cpp
	src/mammut_config.cpp
	src/mammut_fuse.cpp
	src/module.cpp
)

set (HEADERS
	src/communicator.h
	src/mammut_config.h
	src/mammut_fuse.h
	src/module.h
	src/resolver.h
	src/thread_queue.h
)


find_package(Config++ REQUIRED)
find_package(fuse REQUIRED)

add_definitions("-ggdb -Og -pthread -D_FILE_OFFSET_BITS=64 -Wall -Wextra
-pedantic")
add_executable(mammutfs ${SOURCES} ${HEADERS})
set_property(TARGET mammutfs PROPERTY CXX_STANDARD 14)

# for config.h
target_include_directories(mammutfs PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(mammutfs ${CONFIG++_LIBRARY} ${FUSE_LIBRARIES} pthread)

add_subdirectory(src/module)
