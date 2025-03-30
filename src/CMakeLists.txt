file(GLOB SOURCES *.cpp)

add_executable(Re-SC-Masker ${SOURCES})

# Headers
target_include_directories(Re-SC-Masker PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
    ${Z3_CXX_INCLUDE_DIRS}
)

# LLVM Definitions
target_compile_definitions(Re-SC-Masker PRIVATE
    ${LLVM_DEFINITIONS}
)

# Linking
target_link_libraries(Re-SC-Masker PRIVATE
    LLVM
    clangTooling
    clangFrontend
    clangAST
    clangBasic
    ${Z3_LIBRARIES}
)
