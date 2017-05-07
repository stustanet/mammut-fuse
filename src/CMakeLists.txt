cmake_minimum_required(VERSION 2.8)

project (mammutfs)

set (SOURCES
	main.cpp
	mammut_config.cpp
	mammut_fuse.cpp
	options.cpp
	module.cpp
)

set (HEADERS
	resolver.h
	module.h
	mammut_config.h
	mammut_fuse.h
	options.h
)

set (CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/../cmake")

message (${CMAKE_MODULE_PATH})

FIND_PACKAGE(Config++ REQUIRED)
FIND_PACKAGE(fuse REQUIRED)

add_definitions(" -ggdb -Og -D_FILE_OFFSET_BITS=64 ")
add_executable(mammutfs ${SOURCES} ${HEADERS})

target_link_libraries(mammutfs ${CONFIG++_LIBRARY})
target_link_libraries(mammutfs ${FUSE_LIBRARIES})


set_property(TARGET mammutfs PROPERTY CXX_STANDARD 14)

add_subdirectory(module)

