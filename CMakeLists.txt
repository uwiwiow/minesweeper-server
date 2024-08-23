cmake_minimum_required(VERSION 3.28)
project(minesweeper-server C)

set(CMAKE_C_STANDARD 11)

add_executable(server main.c)
target_link_libraries(server raylib GL m pthread dl rt X11)
