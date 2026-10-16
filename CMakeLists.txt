cmake_minimum_required(VERSION 3.16)
project(CmdLine3D LANGUAGES CXX)

add_executable(cmdline3d cmdfps.cpp)
target_compile_features(cmdline3d PRIVATE cxx_std_17)

if(MSVC)
    target_compile_options(cmdline3d PRIVATE /W4 /permissive-)
else()
    target_compile_options(cmdline3d PRIVATE -Wall -Wextra -Wpedantic)
endif()
