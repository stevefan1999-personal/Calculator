cmake_minimum_required (VERSION 2.6)
project (calc)

find_package(BISON)
find_package(FLEX)

bison_target(calc_parser calc.y calc.parse.c)
flex_target(calc_lexer calc.l calc.lex.c)

add_flex_bison_dependency(calc_lexer calc_parser)

add_executable(calc calc.c ${BISON_calc_parser_OUTPUTS} ${FLEX_calc_lexer_OUTPUTS} tgc/tgc.c )
target_link_libraries(calc m)

