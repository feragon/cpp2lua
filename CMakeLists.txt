cmake_minimum_required(VERSION 3.11)
project(cpp2lua)

set(CMAKE_CXX_STANDARD 11)

find_package(LLVM REQUIRED)

include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})


add_executable(cpp2lua main.cpp)
target_link_libraries(cpp2lua
        clangTooling
        clangBasic
        clangAST
        clangASTMatchers
        clangFrontend
        LLVM)