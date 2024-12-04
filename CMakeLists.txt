cmake_minimum_required(VERSION 3.10)
project(LLVMExample)

find_package(LLVM 16 REQUIRED CONFIG)
find_package(Clang 16 REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Clang include dirs: ${CLANG_INCLUDE_DIRS}")

if(CMAKE_BUILD_TYPE MATCHES Debug)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
endif()

# Use LLVM/Clang's include directories
include_directories(${LLVM_INCLUDE_DIRS})
include_directories(${CLANG_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# Add the main file
add_executable(Re-SC-Masker main.cpp)

# Link against libraries
target_link_libraries(Re-SC-Masker PRIVATE LLVM clangTooling clangFrontend clangAST clangBasic)
