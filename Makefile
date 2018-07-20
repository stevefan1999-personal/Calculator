.PHONY: all clean calc.parse.c calc.lex.c

calc.parse.c:	calc.y calc.lex.c
	bison -o calc.parse.c -d calc.y

calc.lex.c: calc.l
	flex calc.l

calc: calc.lex.c calc.parse.c
	gcc -w -o calc calc.c calc.lex.c calc.parse.c -lm

all: clean calc

clean:
	@rm -f calc calc.lex.c calc.lex.h calc.parse.c calc.parse.h