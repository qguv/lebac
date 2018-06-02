
CC=gcc
ERROR_OPTS=-Wall -Wpedantic -Wextra -Werror
LIBS=-Itermbox/src -L./build/termbox/src -ltermbox -lrt
OUTPUT=build/lebac

build/lebac: build/termbox/src/libtermbox.a src/*.c src/*.h Makefile
	${CC} -static -D_POSIX_C_SOURCE=199309L -std=c11 src/main.c ${LIBS} ${ERROR_OPTS} -o $@

build/termbox/src/libtermbox.a: Makefile
	( \
	cd termbox ;\
	echo "configuring termbox" ;\
	./waf configure --out=../build/termbox ;\
	echo "building termbox" ;\
	./waf ;\
	)

clean:
	rm -fr ./build

