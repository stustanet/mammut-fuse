cmake_minimum_required(VERSION 2.8)

project (mammutfs)

set (SOURCES
	src/communicator.cpp
	src/main.cpp
	src/mammut_config.cpp
	src/mammut_fuse.cpp
	src/module.cpp
	src/options.cpp
)

set (HEADERS
	src/communicator.h
	src/mammut_config.h
	src/mammut_fuse.h
	src/module.h
	src/options.h
	src/resolver.h
	src/thread_queue.h
)

set (CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

message (${CMAKE_MODULE_PATH})

FIND_PACKAGE(Config++ REQUIRED)
FIND_PACKAGE(fuse REQUIRED)

add_definitions(" -ggdb -Og -pthread -D_FILE_OFFSET_BITS=64 ")
add_executable(mammutfs ${SOURCES} ${HEADERS})

target_link_libraries(mammutfs ${CONFIG++_LIBRARY})
target_link_libraries(mammutfs ${FUSE_LIBRARIES})
target_link_libraries(mammutfs pthread)

set_property(TARGET mammutfs PROPERTY CXX_STANDARD 14)

add_subdirectory(src/module)

