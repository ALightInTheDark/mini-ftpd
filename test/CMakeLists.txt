cmake_minimum_required(VERSION 3.16)
project(Test)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_library(config ../config/config.cpp)
add_library(file ../file/file_utility.cpp )
add_library(ipc ../ipc/socket_ipc.cpp )
add_library(blocksocket ../socket/block_socket.cpp)

add_executable(test_config test_config.cpp)
target_link_libraries(test_config PRIVATE config)
target_include_directories(test_config PRIVATE ../config)

add_executable(test_file test_file.cpp)
target_link_libraries(test_file PRIVATE file)
target_include_directories(test_file PRIVATE ../file)

add_executable(test_ipc test_ipc.cpp)
target_link_libraries(test_ipc PRIVATE ipc)
target_include_directories(test_ipc PRIVATE ../ipc)

add_executable(test_socket_client test_socket_client.cpp)
add_executable(test_socket_server test_socket_server.cpp)
target_link_libraries(test_socket_client PRIVATE blocksocket)
target_link_libraries(test_socket_server PRIVATE blocksocket)
target_include_directories(test_socket_client PRIVATE ../socket)
target_include_directories(test_socket_server PRIVATE ../socket)