cmake_minimum_required(VERSION 3.16)

if(POLICY CMP0177)
    cmake_policy(SET CMP0177 NEW)
endif()

project(v5_clean_mainline C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)

if(MSVC)
    add_compile_options(/utf-8)
endif()

add_subdirectory(app)
