cmake_minimum_required(VERSION 3.31.2)

project(fkvs-server C)
project(fkvs-cli C)

set(CMake_C_STANDARD, 23)

if(APPLE)
  add_executable(fkvs-server client.c list.c config.c networking.c server.c hashtable.c command_registry.c server_command_handlers.c event_dispatcher_kqueue.c)
  target_compile_definitions(fkvs-server PRIVATE SERVER)
elseif(LINUX)
  add_executable(fkvs-server client.c list.c config.c networking.c server.c hashtable.c command_registry.c server_command_handlers.c event_dispatcher_epoll.c)
  target_compile_definitions(fkvs-server PRIVATE SERVER)
endif()
add_executable(fkvs-cli fkvs-cli.c config.c command_parser.c client_command_handlers.c)
target_compile_definitions(fkvs-cli PRIVATE CLI)