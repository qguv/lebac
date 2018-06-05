CC=gcc
ERROR_OPTS=-Wall -Wpedantic -Wextra -Werror -g
LIBS=-Ibuild/tables -Itermbox/src -Lbuild/termbox/src -ltermbox -lrt
OUTPUT=build/lebac

WAVETABLE_LENGTH=16
SAMPLE_RATE=38000

build/lebac: build/termbox/src/libtermbox.a build/tables/wavehop.h src/*.c src/*.h Makefile
	${CC} -static -D_POSIX_C_SOURCE=199309L -D_DEFAULT_SOURCE -std=c11 src/main.c ${LIBS} ${ERROR_OPTS} -o $@

build/termbox/src/libtermbox.a: Makefile
	mkdir -p build
	( \
	cd termbox &&\
	echo "configuring termbox" &&\
	./waf configure --out=../build/termbox &&\
	echo "building termbox" &&\
	./waf \
	)

build/tables/wavehop.h: scripts/mkwavehop.py
	mkdir -p build/tables
	python3 $< $(WAVETABLE_LENGTH) $(SAMPLE_RATE) > $@ || (rm $@ && false)

clean:
	rm -rf build
