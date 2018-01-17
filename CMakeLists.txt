cmake_minimum_required(VERSION 3.10.0)
project(skcms)

add_library(skcms skcms.c)

enable_testing()
add_executable(tests tests.c)
target_link_libraries(tests skcms)
add_test(tests tests)

add_executable(iccdump iccdump.c)
target_link_libraries(iccdump skcms)

if (${CMAKE_C_COMPILER_ID} MATCHES "Clang")
    target_compile_options(skcms   PRIVATE -Weverything -Werror -Wno-padded -fcolor-diagnostics)
    target_compile_options(tests   PRIVATE -Weverything -Werror -Wno-padded -fcolor-diagnostics)
    target_compile_options(iccdump PRIVATE -Weverything -Werror -Wno-padded -fcolor-diagnostics)
elseif (${CMAKE_C_COMPILER_ID} MATCHES "GNU")
    target_compile_options(skcms   PRIVATE -Wall -Wextra -Werror -Wno-abi -fdiagnostics-color)
    target_compile_options(tests   PRIVATE -Wall -Wextra -Werror -Wno-abi -fdiagnostics-color)
    target_compile_options(iccdump PRIVATE -Wall -Wextra -Werror -Wno-abi -fdiagnostics-color)
elseif (MSVC)
    target_compile_options(skcms   PRIVATE /W4 /WX /wd4200)
    target_compile_options(tests   PRIVATE /W4 /WX)
    target_compile_options(iccdump PRIVATE /W4 /WX)
endif()

