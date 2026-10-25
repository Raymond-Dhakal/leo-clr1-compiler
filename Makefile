# Leo compiler + original CLR(1) parser generator.
#
#   make            build ./leoc (the compiler) and ./clr_parser (the generator tool)
#   make test       build and run the whole test suite
#   make clean
CC      ?= gcc
CFLAGS  ?= -O2
WARN    := -Wall -Wextra
INC     := -Iinclude
GRAMMAR := $(abspath language/grammar.txt)

GEN_SRC  := src/grammar.c src/lexer.c src/first.c src/lr1_item.c src/closure.c \
            src/goto.c src/state.c src/canonical.c src/parsing_table.c \
            src/conflict.c src/parser.c
LEO_SRC := $(wildcard src/compiler/nova_*.c)

all: leoc clr_parser

leoc: $(GEN_SRC) $(LEO_SRC) src/compiler/leoc.c $(wildcard include/*.h)
	$(CC) $(CFLAGS) $(WARN) $(INC) -DLEO_DEFAULT_GRAMMAR='"$(GRAMMAR)"' -o $@ \
	    $(GEN_SRC) $(LEO_SRC) src/compiler/leoc.c -lm

clr_parser: $(GEN_SRC) src/main.c $(wildcard include/*.h)
	$(CC) $(CFLAGS) $(WARN) $(INC) -o $@ $(GEN_SRC) src/main.c

test: leoc clr_parser
	./tests/run_tests.sh

web:
	python3 tools/make_web.py

rag-index:
	python3 -m rag.indexer

serve: web rag-index
	python3 -m rag.server

wasm:
	bash tools/build_wasm.sh

clean:
	rm -f leoc novac clr_parser
	rm -rf output build

.PHONY: all test clean web rag-index serve wasm
