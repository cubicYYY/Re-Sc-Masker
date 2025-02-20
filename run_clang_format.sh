git ls-files "*.cpp" "*.h" | grep -Ev "^(input/|output/)" | xargs clang-format -i
