cmake_minimum_required(VERSION 3.10.0)
project(skcms)

add_library(skcms skcms.c)

enable_testing()
add_executable(tests tests.c)
target_link_libraries(tests skcms)
add_test(tests tests)
