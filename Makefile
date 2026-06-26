.DEFAULT_GOAL=main

prog=pwu
cc=gcc
cflags=-Wall -Wextra -Werror
cflags+=-std=c2x
srcs=\
	pwu.c

main:
	$(cc) $(cflags) $(srcs) -o $(prog)
