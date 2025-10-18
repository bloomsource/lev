cmake_minimum_required(VERSION 2.9)

project( echo_server )

# set the C++ standard to C++ 11
set(CMAKE_CXX_STANDARD 11)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN)
    add_definitions(-D_CRT_SECURE_NO_WARNINGS)
    add_definitions(-D_WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

set( srcs lev.cpp lev_con.cpp dynbuf.cpp mempool.cpp util.cpp )
add_executable(echo_server main.cpp client.cpp server.cpp ${srcs} )

