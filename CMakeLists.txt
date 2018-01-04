cmake_minimum_required(VERSION 3.10.0)
project(skcms)

add_library(skcms skcms.c)

enable_testing()
add_executable(tests tests.c)
target_link_libraries(tests skcms)
add_test(tests tests)

if (${CMAKE_C_COMPILER_ID} MATCHES "Clang")
    target_compile_options(skcms PRIVATE -Weverything -Werror -fcolor-diagnostics)
    target_compile_options(tests PRIVATE -Weverything -Werror -fcolor-diagnostics)
elseif (${CMAKE_C_COMPILER_ID} MATCHES "GNU")
    target_compile_options(skcms PRIVATE -Wall -Wextra -Werror -fdiagnostics-color)
    target_compile_options(tests PRIVATE -Wall -Wextra -Werror -fdiagnostics-color)
elseif (MSVC)
    target_compile_options(skcms PRIVATE /Wall /WX)
    target_compile_options(tests PRIVATE /Wall /WX)
endif()

