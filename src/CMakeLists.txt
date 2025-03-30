file(GLOB SOURCES *.cpp)

add_executable(Re-Sc-Masker ${SOURCES})

# Headers
target_include_directories(Re-Sc-Masker PRIVATE
    ${LLVM_INCLUDE_DIRS}
    ${CLANG_INCLUDE_DIRS}
    ${Z3_CXX_INCLUDE_DIRS}
)

# LLVM Definitions
target_compile_definitions(Re-Sc-Masker PRIVATE
    ${LLVM_DEFINITIONS}
)

# Linking
target_link_libraries(Re-Sc-Masker PRIVATE
    LLVM
    clangTooling
    clangFrontend
    clangAST
    clangBasic
    ${Z3_LIBRARIES}
)
