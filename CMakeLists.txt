cmake_minimum_required(VERSION 3.10)
project(ReSCMasker)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Ensure C++ standard is set
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find Essential Libs
find_package(LLVM 16 REQUIRED CONFIG)
find_package(Clang 16 REQUIRED CONFIG)
find_package(Z3 REQUIRED)

# Messages for feedback
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Clang include dirs: ${CLANG_INCLUDE_DIRS}")
message(STATUS "Z3 Library: ${Z3_LIBRARIES}")

if(CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "Debug mode enabled")
    set(CMAKE_CXX_FLAGS_DEBUG "-g")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

add_subdirectory(src)