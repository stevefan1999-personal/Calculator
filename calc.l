%option outfile="calc.lex.c"
%option header-file="calc.lex.h"
%option prefix="calc_"
%option extra-type="void *"
%option noyywrap noyyalloc noyyfree noyyrealloc nodefault
%option bison-bridge bison-locations
%option warn 8bit reentrant ecs
%option yylineno
%option noline


%{
#define YYPOLLUTE_NAMESPACE
#include "calc.parse.h"

#define YY_USER_ACTION do { \
    yylloc->first_line = yylloc->last_line = yylineno; \
    yylloc->first_column = yycolumn; \
    yylloc->last_column = yycolumn + yyleng - 1; \
    yycolumn += yyleng; \
  } while (0);

/*
 * little cheat sheet:
 * yytext, yyleng, yyval, yylloc, yyscanner
 */

extern int isatty(int fd);
%}

NUMBER [0-9]+|[0-9]*\.?[0-9]*([eE][-+]?[0-9]+)?
STRING (\"(\\.|[^"\\])*\")|(\'(\\.|[^'\\])*\')
IDENTIFIER [_a-zA-Z][_a-zA-Z0-9]*

%%

"exit" YIELD(T_QUIT);
"quit" YIELD(T_QUIT);

{STRING} {
  size_t len = strlen(yytext);
  char *mem = memcpy(yyalloc(len + 1, yyscanner), yytext, len);
  mem[len] = '\0';
  yylval->string = mem;
  YIELD(T_STRING);
}

{IDENTIFIER} {
  size_t len = strlen(yytext);
  char *mem = memcpy(yyalloc(len + 1, yyscanner), yytext, len);
  mem[len] = '\0';
  yylval->identifier = mem;
  YIELD(T_IDENTIFIER);
}

{NUMBER} {
  double d = atof(yytext);
  yylval->value = memcpy(yyalloc(sizeof(double), yyscanner), &d, sizeof(double));
  YIELD(T_NUMBER);
}

\r?\n YIELD(T_NEWLINE);
"++" YIELD(T_INCREMENT);
"--" YIELD(T_DECREMENT);
"+=" YIELD(T_PLUS_EQUAL);
"-=" YIELD(T_MINUS_EQUAL);
"*=" YIELD(T_MUL_EQUAL);
"/=" YIELD(T_DIV_EQUAL);
"%=" YIELD(T_MOD_EQUAL);
"==" YIELD(T_LOGICAL_EQUAL);
"!=" YIELD(T_NOT_EQUAL);
">=" YIELD(T_GT_EQUAL);
"<=" YIELD(T_LT_EQUAL);
"||" YIELD(T_OR);
"&&" YIELD(T_AND);
"^^" YIELD(T_EXP);
"=" YIELD(T_EQUAL);
"+" YIELD(T_PLUS);
"-" YIELD(T_MINUS);
"*" YIELD(T_MUL);
"/" YIELD(T_DIV);
"%" YIELD(T_MOD);
"^" YIELD(T_CARET);
"," YIELD(T_COMMA);
"(" YIELD(T_LPAREN);
")" YIELD(T_RPAREN);
"<" YIELD(T_LT);
">" YIELD(T_GT);
"!" YIELD(T_NOT);

[ |\t] {;}

. {
  yylval->character = yytext[0];
  YIELD(T_CHARACTER);
}
%%

#if 0
// CONTROL ALL THE MEMORY
void *yyalloc(size_t bytes, void *scanner) {
    return malloc(bytes);
}

void *yyrealloc(void *ptr, size_t bytes, void *scanner) {
    return realloc(ptr, bytes);
}

void yyfree(void *ptr, void *scanner) {
    free(ptr);
}
#endif