cmake_minimum_required(VERSION 3.0)

## DONT: The ``target_sources()`` command converts relative paths to absolute.
cmake_policy(SET CMP0076 NEW)

project (mammutfs)

set (CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")


add_subdirectory(src)
