cmake_minimum_required(VERSION 3.5)
project(JitRepro)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(LLVM REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
llvm_map_components_to_libnames(llvm_libs support core irreader bitwriter)

add_executable(main main.cpp)
target_include_directories(main PRIVATE ${LLVM_INCLUDE_DIRS})
target_link_libraries(main PRIVATE LLVM)

add_library(runtime SHARED runtime.cpp)
target_link_libraries(runtime PRIVATE ze_loader)

file(COPY main.ll DESTINATION ${CMAKE_CURRENT_BINARY_DIR})