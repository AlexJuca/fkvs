cmake_minimum_required(VERSION 3.31.2)

add_subdirectory("deps/linenoise")

project(fkvs-server C)
project(fkvs-cli C)
project(fkvs-benchmark C)

set(CMake_C_STANDARD, 23)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

if(APPLE)
  add_executable(fkvs-server client.c list.c config.c networking.c server.c hashtable.c command_registry.c server_command_handlers.c event_dispatcher_kqueue.c)
  target_compile_definitions(fkvs-server PRIVATE SERVER)

  add_executable(fkvs-benchmark fkvs-benchmark.c client.c list.c networking.c command_parser.c client_command_handlers.c)
  target_compile_definitions(fkvs-benchmark PRIVATE CLI)
elseif(LINUX)
  add_executable(fkvs-server client.c list.c config.c networking.c server.c hashtable.c command_registry.c server_command_handlers.c event_dispatcher_epoll.c)
  target_compile_definitions(fkvs-server PRIVATE SERVER)

  add_executable(fkvs-benchmark fkvs-benchmark.c client.c list.c networking.c command_parser.c client_command_handlers.c)
  target_compile_definitions(fkvs-benchmark PRIVATE CLI)
endif()
  add_executable(fkvs-cli fkvs-cli.c config.c command_parser.c client_command_handlers.c)
  target_link_libraries(fkvs-benchmark PRIVATE Threads::Threads)
  target_link_libraries(fkvs-cli PUBLIC linenoise)
  target_compile_definitions(fkvs-cli PRIVATE CLI)
