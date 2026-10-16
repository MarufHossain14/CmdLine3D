cmake_minimum_required(VERSION 3.16)
project(CmdLine3D LANGUAGES CXX)

find_package(SDL3 CONFIG REQUIRED)

add_executable(cmdline3d cmdfps.cpp)
target_compile_features(cmdline3d PRIVATE cxx_std_17)
target_link_libraries(cmdline3d PRIVATE SDL3::SDL3)

if(MSVC)
    target_compile_options(cmdline3d PRIVATE /W4 /permissive-)
else()
    target_compile_options(cmdline3d PRIVATE -Wall -Wextra -Wpedantic)
endif()
