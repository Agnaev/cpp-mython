cmake_minimum_required(VERSION 3.8)
project(mython)

set(CMAKE_CXX_STANDARD 17)

file(GLOB sources "mython/*.h" "mython/*.cpp")

add_executable(mython ${sources})
