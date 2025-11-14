cmake_minimum_required(VERSION 3.31.2)

add_subdirectory("deps/linenoise")

project(fkvs-server C)
project(fkvs-cli C)
project(fkvs-benchmark C)

set(CMake_C_STANDARD, 23)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if(APPLE)
  add_executable(fkvs-server src/memory.c src/counter.c src/client.c src/core/list.c src/config.c src/networking/networking.c src/server.c src/core/hashtable.c src/commands/common/command_registry.c src/commands/server/server_command_handlers.c src/io/event_dispatcher_kqueue.c)
  target_compile_definitions(fkvs-server PRIVATE SERVER)

  add_executable(fkvs-benchmark src/string_utils.c src/fkvs-benchmark.c src/client.c src/core/list.c src/networking/networking.c src/commands/common/command_parser.c src/commands/client/client_command_handlers.c)
  target_compile_definitions(fkvs-benchmark PRIVATE CLI)
elseif(LINUX)
  find_library(LIBURING liburing)

  if(LIBURING)
    add_executable(fkvs-server src/memory.c src/counter.c src/client.c src/core/list.c src/config.c src/networking/networking.c src/server.c src/core/hashtable.c src/commands/common/command_registry.c src/commands/server/server_command_handlers.c src/io/event_dispatcher_io_uring.c)
    target_link_libraries(fkvs-server PRIVATE LIBURING)
    target_compile_definitions(fkvs-server PRIVATE SERVER IO_URING_ENABLED)
  else()
    add_executable(fkvs-server src/memory.c src/counter.c src/client.c src/core/list.c src/config.c src/networking/networking.c src/server.c src/core/hashtable.c src/commands/common/command_registry.c src/commands/server/server_command_handlers.c src/io/event_dispatcher_epoll.c)
    target_compile_definitions(fkvs-server PRIVATE SERVER)
  endif()

  add_executable(fkvs-benchmark src/string_utils.c src/fkvs-benchmark.c src/client.c src/core/list.c src/networking/networking.c src/commands/common/command_parser.c src/commands/client/client_command_handlers.c)
  target_compile_definitions(fkvs-benchmark PRIVATE CLI)
endif()

# Add test executable
add_executable(test_counter tests/test_counter.c src/counter.c)
add_executable(test_string_utils tests/test_string_utils.c src/string_utils.c)
target_link_libraries(test_counter)
target_link_libraries(test_string_utils)

# Enable testing
enable_testing()
add_test(NAME CounterTest COMMAND test_counter)
add_test(NAME StringUtilsTest COMMAND test_string_utils)
add_executable(fkvs-cli src/string_utils.c src/fkvs-cli.c src/config.c src/commands/common/command_parser.c src/commands/client/client_command_handlers.c)
  target_link_libraries(fkvs-benchmark PRIVATE Threads::Threads)
  target_link_libraries(fkvs-cli PUBLIC linenoise)
  target_compile_definitions(fkvs-cli PRIVATE CLI)
