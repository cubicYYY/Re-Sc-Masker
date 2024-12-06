cmake_minimum_required(VERSION 3.10)
project(ReSCMasker)

# Find LLVM and Clang
find_package(LLVM 16 REQUIRED CONFIG)
find_package(Clang 16 REQUIRED CONFIG)

# Messages for feedback
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")
message(STATUS "Clang include dirs: ${CLANG_INCLUDE_DIRS}")

# Ensure debug flags for Debug builds
if(CMAKE_BUILD_TYPE MATCHES Debug)
    message(STATUS "Debug mode enabled")
    set(CMAKE_CXX_FLAGS_DEBUG "-g")
endif()

# Gather all .cpp files in the current directory
file(GLOB SOURCES *.cpp)

# Add executable with all gathered sources
add_executable(Re-SC-Masker ${SOURCES})


# Include LLVM and Clang headers
target_include_directories(Re-SC-Masker PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
)

# Add LLVM and Clang definitions
target_compile_definitions(Re-SC-Masker PRIVATE
    ${LLVM_DEFINITIONS}
)

# Link against LLVM and Clang libraries
target_link_libraries(Re-SC-Masker PRIVATE
    LLVM
    clangTooling
    clangFrontend
    clangAST
    clangBasic
)

# Ensure C++ standard is set
set_target_properties(Re-SC-Masker PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)
