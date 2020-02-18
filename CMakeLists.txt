cmake_minimum_required(VERSION 3.7)
project(namedb LANGUAGES C VERSION 0.0.1)

# Until CPU extensions can be checked from CMake set defines manually.

add_library(namedb SHARED src/dname.c src/simd.c src/tree.c)
target_compile_definitions(namedb PUBLIC HAVE_SSE2=1 HAVE_AVX2=1)
target_compile_options(namedb PUBLIC -mavx2)

add_executable(test src/main.c)
target_link_libraries(test PRIVATE namedb)
