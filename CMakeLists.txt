cmake_minimum_required(VERSION 3.16)
project(omnimsg_min C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(omnimsg
  src/omnimsg.c
)

if (WIN32)
  target_link_libraries(omnimsg ws2_32)
  target_compile_definitions(omnimsg PRIVATE _CRT_SECURE_NO_WARNINGS _WINSOCK_DEPRECATED_NO_WARNINGS)
endif()

if (MSVC)
  target_compile_options(omnimsg PRIVATE /W4)
else()
  target_compile_options(omnimsg PRIVATE -Wall -Wextra -Wpedantic)
endif()
