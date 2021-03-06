cmake_minimum_required (VERSION 2.6)
project (calc)

find_package(BISON)
find_package(FLEX)


if (UNIX AND NOT APPLE)
    set(BUILD_SHARED_LIBS OFF)
endif ()

set(CMAKE_CXX_FLAGS_DEBUG "-g -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELEASE "-O3")
set(CMAKE_C_FLAGS_DEBUG "-g -Wall -Wextra")
set(CMAKE_C_FLAGS_RELEASE "-O3")


bison_target(calc_parser calc.y calc.parse.c)
flex_target(calc_lexer calc.l calc.lex.c)

add_flex_bison_dependency(calc_lexer calc_parser)

add_library(tgc tgc/tgc.c)

add_executable(calc calc.c ${BISON_calc_parser_OUTPUTS} ${FLEX_calc_lexer_OUTPUTS})
target_link_libraries(calc m tgc)

