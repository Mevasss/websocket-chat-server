cmake_minimum_required(VERSION 3.10)
project(ChatServer)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_executable(chat_server
    src/main.cpp
    src/websocket_server.cpp
)

target_include_directories(chat_server PRIVATE ${OPENSSL_INCLUDE_DIR})
target_link_libraries(chat_server ${OPENSSL_LIBRARIES} Threads::Threads)
