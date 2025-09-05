cmake_minimum_required(VERSION 3.31.2)

project(fvks C)

project(fkvs-cli C)

set(CMake_C_STANDARD, 23)

if(APPLE)
  add_executable(fkvs server.c event_dispatcher_kqueue.c hashtable.c command_registry.c command_handlers.c)
elseif(LINUX)
  add_executable(fkvs server.c event_dispatcher_epoll.c hashtable.c command_registry.c command_handlers.c)
endif()
add_executable(fkvs-cli fkvs-cli.c config.c command_parser.c)