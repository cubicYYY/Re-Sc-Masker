git ls-files \
    "src/*.c" \
    "src/*.cpp" \
    "src/*.h" \
    "src/*.hpp" \
    "include/*.c" \
    "include/*.cpp" \
    "include/*.h" \
    "include/*.hpp" \
| xargs clang-format -i
