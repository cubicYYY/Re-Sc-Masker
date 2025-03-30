cmake_minimum_required(VERSION 3.10)
project(ReSCMasker)

# Ensure C++ standard is set
set(CMAKE_CXX_STANDARD 20)
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

add_subdirectory(src)