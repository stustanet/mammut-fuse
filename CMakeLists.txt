cmake_minimum_required(VERSION 3.0)

## DONT: The ``target_sources()`` command converts relative paths to absolute.
cmake_policy(SET CMP0076 NEW)

## ``target_link_libraries()`` allows use with targets in other directories.
cmake_policy(SET CMP0079 NEW)


project (mammutfs)

set (CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

find_package(Config++ REQUIRED)
find_package(fuse REQUIRED)

add_definitions("-ggdb -Og -pthread -D_FILE_OFFSET_BITS=64 -Wall -Wextra
-pedantic")

add_executable(mammutfs)

add_subdirectory(src)
