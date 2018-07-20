%require "3.0"
%locations
%defines
%no-lines
%verbose
%glr-parser

// definitions
%define api.pure true
%define api.prefix {calc_}
%define api.token.prefix {calc_token_}

%define parse.trace true
%define parse.error verbose

%param {void *yyscanner}
%code {}
%code requires {
#define YY_NO_UNISTD_H

#ifdef YYPOLLUTE_NAMESPACE

// this 'prefix' must match api.token.prefix
# define YYPREFIX_TOKEN(x) calc_token_##x

// these two 'prefix' macros must matches your api.prefix as well
# define YYPREFIX_MACRO(x) CALC_##x
# define YYPREFIX(x) calc_##x

// hacks to resolve undefined YYSTYPE, YYLTYPE and different yylex on flex
# ifndef YYSTYPE
#  define YYSTYPE YYPREFIX_MACRO(STYPE)
# endif

# ifndef YYLTYPE
#  define YYLTYPE YYPREFIX_MACRO(LTYPE)
# endif

# ifndef yyerror
#  define yyerror YYPREFIX(error)
# endif

# ifndef yyalloc
#  define yyalloc YYPREFIX(alloc)
# endif

# ifndef yyrealloc
#  define yyrealloc YYPREFIX(realloc)
# endif

# ifndef yyfree
#  define yyfree YYPREFIX(free)
# endif

# ifndef yy_set_extra
#  define yy_set_extra YYPREFIX(set_extra)
# endif

# ifndef yy_get_extra
#  define yy_get_extra YYPREFIX(get_extra)
# endif

#endif
}
%code provides {
typedef struct node {
	const char *op;
	struct node *left;
	struct node *right;
} node;

#define YIELD(x) return YYPREFIX_TOKEN(x)

void yyerror (YYLTYPE *yylloc, void *yyscanner, char const *msg);
}

%{

#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>

#define YYDEBUG 0
#define YYPOLLUTE_NAMESPACE
#include "calc.parse.h"
#include "calc.lex.h"

#if defined (alloca) || defined (_ALLOCA_H)
# define YYSTACK_ALLOC alloca
#elif defined (__GNUC__)
# define YYSTACK_ALLOC __builtin_alloca
#else
# undef YYSTACK_ALLOC
#endif

// #define YYMALLOC(size) yyalloc(size, yyscanner)
// #define YYFREE(ptr) yyfree(ptr, yyscanner)

struct node *make_node(void *yyscanner, const char *op, struct node *left, struct node *right);
void free_ast(void *yyscanner, struct node *ast);

double eval_ast(struct node *ast, size_t level);

/*
 * little cheat sheet:
 * yytext, yyleng, yyval
 */

#if YYDEBUG
int yydebug = 1;
#endif

%}

// syntax union
%union {
	double *value;
	char character;
	const char *string;
	const char *identifier;
	struct node *node;
}

%token<character> T_CHARACTER

// numbers
%token<value> T_NUMBER "number"

// identifier
%token<identifier> T_IDENTIFIER "identifier"

// string
%token<string> T_STRING "string"

// operators
%token
	T_PLUS
	T_MINUS
	T_MUL
	T_DIV
	T_LEFT
	T_RIGHT
	T_PLUS_EQUAL
	T_MINUS_EQUAL
	T_MUL_EQUAL
	T_DIV_EQUAL
	T_MOD_EQUAL
	T_EQUAL
	T_INCREMENT
	T_DECREMENT
	T_COMMA
	T_LOGICAL_EQUAL
	T_NOT_EQUAL
	T_GT_EQUAL
	T_LT_EQUAL
	T_LT
	T_GT
	T_NOT
	T_AND
	T_OR
	T_MOD
	T_EXP

// punctuations
%token
	T_NEWLINE
	T_QUIT

// left associative
%left
	T_COMMA
	T_PLUS
	T_MINUS
	T_MUL
	T_DIV
	T_MOD
	T_LOGICAL_EQUAL
	T_NOT_EQUAL
	T_GT_EQUAL
	T_LT_EQUAL
	T_LT
	T_GT
	T_AND
	T_OR
	T_INCREMENT_SUFFIX
	T_DECREMENT_SUFFIX

// right associative
%right
	T_CARET
	T_NOT
	T_POS
	T_NEG
	T_EQUAL
	T_PLUS_EQUAL
	T_MINUS_EQUAL
	T_MUL_EQUAL
	T_DIV_EQUAL
	T_MOD_EQUAL
	T_EXP
	T_FAC
	T_INCREMENT
	T_DECREMENT

%nonassoc
	T_LPAREN
	T_RPAREN

%type<value> number;
%type<string> string;
%type<identifier> identifier;

%type<node>
	expr
	assignment_expr
	additive_expr
	multiplicative_expr
	primary_expr
	identifier_expr
	comma_expr
	unary_expr
	relational_expr
	logical_and_expr
	logical_or_expr
	exponential_expr
	suffix_operator_expr
	factorial_expr
;

%printer { fprintf (yyoutput, "'%c'", $$); } <character>

%destructor {
	if ($$ != NULL) {
		free_ast(yyscanner, $$);
	}
} <node>;

%start line

%%

string: T_STRING { $$ = $1; };
number: T_NUMBER { $$ = $1; };
identifier: T_IDENTIFIER { $$ = $1; };

line: T_NEWLINE { YYACCEPT; }
| expr T_NEWLINE {
	printf("\t[expr: %.16g]\n", eval_ast($1, 0));
	free_ast(yyscanner, $1);
	YYACCEPT;
}
|	T_QUIT T_NEWLINE {
	printf("bye!\n");
	yy_set_extra((void *)1, yyscanner);
	YYACCEPT;
}
;

identifier_expr:
	identifier[iden] { $$ = make_node(yyscanner, "identifier", (struct node *) $iden, NULL); };

primary_expr: identifier_expr { $$ = $identifier_expr; }
| number { $$ = make_node(yyscanner, "number", (struct node *) $number, NULL); }
| string { $$ = make_node(yyscanner, "string", (struct node *) $string, NULL); }
| T_LPAREN expr T_RPAREN { $$ = $expr; }
| identifier_expr T_LPAREN expr T_RPAREN {
	$$ = make_node(yyscanner, "function", $identifier_expr, $expr);
}
;

suffix_operator_expr: primary_expr { $$ = $primary_expr; }
| identifier_expr T_INCREMENT %prec T_INCREMENT_SUFFIX {
	double x = 1;
	double *mem = memcpy(yyalloc(sizeof(double), yyscanner), &x, sizeof(double));
	$$ = make_node(yyscanner, "=",
		$identifier_expr,
		make_node(yyscanner, "+",
			$identifier_expr,
			make_node(yyscanner, "number", (struct node *) mem, NULL)
		)
	);
}
| identifier_expr T_DECREMENT %prec T_DECREMENT_SUFFIX {
	double x = 1;
	double *mem = memcpy(yyalloc(sizeof(double), yyscanner), &x, sizeof(double));
	$$ = make_node(yyscanner, "=",
		$identifier_expr,
		make_node(yyscanner, "-",
			$identifier_expr,
			make_node(yyscanner, "number", (struct node *) mem, NULL)
		)
	);
}
;

factorial_expr: suffix_operator_expr { $$ = $suffix_operator_expr; }
| factorial_expr T_NOT %prec T_FAC { $$ = make_node(yyscanner, "!!", $1, NULL); }
;

exponential_expr: factorial_expr { $$ = $factorial_expr; }
| exponential_expr T_EXP factorial_expr { $$ = make_node(yyscanner, "^^", $1, $factorial_expr); }
;

unary_expr: exponential_expr { $$ = $exponential_expr; }
| T_INCREMENT identifier_expr {
	double x = 1;
	double *mem = memcpy(yyalloc(sizeof(double), yyscanner), &x, sizeof(x));
	$$ = make_node(yyscanner, "=",
		$identifier_expr,
		make_node(yyscanner, "+",
			$identifier_expr,
			make_node(yyscanner, "number", (struct node *) mem, NULL)
		)
	);
}
| T_DECREMENT identifier_expr {
	double x = 1;
	double *mem = memcpy(yyalloc(sizeof(double), yyscanner), &x, sizeof(double));
	$$ = make_node(yyscanner, "=",
		$identifier_expr,
		make_node(yyscanner, "-",
			$identifier_expr,
			make_node(yyscanner, "number", (struct node *) mem, NULL)
		)
	);
}
| T_PLUS exponential_expr %prec T_POS {
	$$ = make_node(yyscanner, "+", $exponential_expr, NULL); }
| T_MINUS exponential_expr %prec T_NEG {
	$$ = make_node(yyscanner, "-", $exponential_expr, NULL); }
| T_NOT exponential_expr {
	$$ = make_node(yyscanner, "!", $exponential_expr, NULL); }
;

multiplicative_expr: unary_expr { $$ = $unary_expr; }
| multiplicative_expr[a] T_MUL unary_expr[b] { $$ = make_node(yyscanner, "*", $a, $b); }
| multiplicative_expr[a] T_DIV unary_expr[b] { $$ = make_node(yyscanner, "/", $a, $b); }
| multiplicative_expr[a] T_MOD unary_expr[b] { $$ = make_node(yyscanner, "%", $a, $b); }
;

additive_expr: multiplicative_expr { $$ = $multiplicative_expr; }
|	additive_expr[a] T_PLUS multiplicative_expr[b] {
	$$ = make_node(yyscanner, "+", $a, $b); }
| additive_expr[a] T_MINUS multiplicative_expr[b] {
	$$ = make_node(yyscanner, "-", $a, $b); }
;

relational_expr: additive_expr { $$ = $1; }
| relational_expr T_LOGICAL_EQUAL additive_expr { $$ = make_node(yyscanner, "==", $1, $3); }
| relational_expr T_GT additive_expr { $$ = make_node(yyscanner, ">", $1, $3); }
| relational_expr T_LT additive_expr { $$ = make_node(yyscanner, "<", $1, $3); }
| relational_expr T_GT_EQUAL additive_expr { $$ =
	make_node(yyscanner, "||",
		make_node(yyscanner, ">", $1, $3),
		make_node(yyscanner, "==", $1, $3)
	);
}
| relational_expr T_LT_EQUAL additive_expr { $$ =
	make_node(yyscanner, "||",
		make_node(yyscanner, "<", $1, $3),
		make_node(yyscanner, "==", $1, $3)
	);
}
| relational_expr T_NOT_EQUAL additive_expr { $$ =
	make_node(yyscanner, "!", make_node(yyscanner, "==", $1, $3), NULL);
}
;

logical_and_expr: relational_expr { $$ = $1; }
| logical_and_expr T_AND relational_expr { $$ = make_node(yyscanner, "&&", $1, $3); }
;

logical_or_expr: logical_and_expr { $$ = $1; }
| logical_or_expr T_OR logical_and_expr { $$ = make_node(yyscanner, "||", $1, $3); }
;

assignment_expr: logical_or_expr { $$ = $1; }
|	identifier_expr T_EQUAL assignment_expr { $$ = make_node(yyscanner, "=", $1, $3); }
|	identifier_expr T_PLUS_EQUAL assignment_expr { $$ =
	make_node(yyscanner, "=", $1, make_node(yyscanner, "+", $1, $3)); }
|	identifier_expr T_MINUS_EQUAL assignment_expr { $$ =
	make_node(yyscanner, "=", $1, make_node(yyscanner, "-", $1, $3)); }
|	identifier_expr T_MUL_EQUAL assignment_expr { $$ =
	make_node(yyscanner, "=", $1, make_node(yyscanner, "*", $1, $3)); }
|	identifier_expr T_DIV_EQUAL assignment_expr { $$ =
	make_node(yyscanner, "=", $1, make_node(yyscanner, "/", $1, $3)); }
|	identifier_expr T_MOD_EQUAL assignment_expr { $$ =
	make_node(yyscanner, "=", $1, make_node(yyscanner, "%", $1, $3)); }
;

comma_expr: assignment_expr { $$ = $1; }
| comma_expr T_COMMA assignment_expr { $$ = make_node(yyscanner, ",", $1, $3); }
;

expr: comma_expr { $$ = $1; }
;

%%

void yyerror (YYLTYPE *yylloc, void *yyscanner, char const *msg) {
	printf("parse error: %s\n", msg);
	printf("%i %i\n", yylloc->first_line, yylloc->first_column);
	fflush(stdout);
}

