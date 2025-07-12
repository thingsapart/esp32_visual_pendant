#!/bin/sh
find src -name "*.c" -o -name "*.h" -o -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i
