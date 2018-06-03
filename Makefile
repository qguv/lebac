CC=gcc
ERROR_OPTS=-Wall -Wpedantic -Wextra -Werror
LIBS=-Ibuild/tables -Itermbox/src -Lbuild/termbox/src -ltermbox -lrt
OUTPUT=build/lebac

SINTABLE_LENGTH=512
SAMPLE_RATE=38000

build/lebac: build/termbox/src/libtermbox.a build/tables/sinhop.h build/tables/sintable.h src/*.c src/*.h Makefile
	${CC} -static -D_POSIX_C_SOURCE=199309L -std=c11 src/main.c ${LIBS} ${ERROR_OPTS} -o $@

build/termbox/src/libtermbox.a: Makefile
	mkdir -p build
	( \
	cd termbox &&\
	echo "configuring termbox" &&\
	./waf configure --out=../build/termbox &&\
	echo "building termbox" &&\
	./waf \
	)

build/tables/sinhop.h: scripts/mksinhop.py
	mkdir -p build/tables
	python3 $< $(SINTABLE_LENGTH) $(SAMPLE_RATE) > $@ || (rm $@ && false)

build/tables/sintable.h: scripts/mksintable.py
	mkdir -p build/tables
	python3 $< $(SINTABLE_LENGTH) > $@ || (rm $@ && false)

clean:
	rm -rf build
