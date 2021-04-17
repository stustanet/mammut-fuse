
set(SAVE_FILE_HANDLES YES CACHE BOOL
	"limit the amount of file handles used.")

set(ENABLE_TRACELOG YES CACHE BOOL
	"Enable verbose trace logging of every syscall")

set(CONFIG_DEBUG NO CACHE BOOL
	"Enable verbose debugging of config values")

set(TRACE_GETATTR NO CACHE BOOL
	"enable also getattr tracelog (very noisy)")

set(ENABLE_WRITE_NOTIFY NO CACHE BOOL
	"Send WRITE (thousands) notifications to mammutfsd.")

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.in
	${CMAKE_CURRENT_BINARY_DIR}/config.h)

set_property(TARGET mammutfs PROPERTY CXX_STANDARD 14)

target_sources(mammutfs PRIVATE 
	communicator.cpp
	main.cpp
	mammut_config.cpp
	mammut_fuse.cpp
	module.cpp
)

target_sources(mammutfs INTERFACE
	communicator.h
	mammut_config.h
	mammut_fuse.h
	module.h
	resolver.h
	thread_queue.h
)


# for config.h
target_include_directories(mammutfs PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(mammutfs ${CONFIG++_LIBRARY} ${FUSE_LIBRARIES} pthread)

add_subdirectory(module)
