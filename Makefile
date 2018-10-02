CC = gcc
ERROR_OPTS = -Wall -Wpedantic -Wextra

# build options
OUTCONFIG = -static -Os
INCLUDE = -Isrc -Ibuild/tables -Itermbox/src
LIBS = -Lbuild/termbox/src -ltermbox -lrt

# source
OBJ = build/obj/disk.o
SOURCEOPTS = -D_POSIX_C_SOURCE=199309L -D_DEFAULT_SOURCE -std=c11

# wave script options
WAVETABLE_LENGTH = 16
SAMPLE_RATE = 38000

build/lebac: build/termbox/src/libtermbox.a build/tables/wavehop.h src/main.c ${OBJ} src/*.h Makefile
	${CC} ${OUTCONFIG} ${SOURCEOPTS} src/main.c ${INCLUDE} ${LIBS} ${ERROR_OPTS} ${OBJ} -o $@

build/obj/%.o: src/%.c
	mkdir -p build/obj
	${CC} ${OUTCONFIG} ${SOURCEOPTS} -c -o $@ $< ${INCLUDE} ${LIBS} ${ERROR_OPTS}

build/termbox/src/libtermbox.a: Makefile
	mkdir -p build
	( \
	cd termbox &&\
	echo "patching termbox waf version" &&\
	( \
		./waf configure --out=../build/termbox > /dev/null 2>&1; \
		sed -i '/raise StopIteration/d' .waf*/waflib/Node.py \
	) && \
	echo "configuring termbox" &&\
	./waf configure --out=../build/termbox > /dev/null 2>&1 &&\
	echo "building termbox" &&\
	./waf \
	)

build/tables/wavehop.h: scripts/mkwavehop.py
	mkdir -p build/tables
	python3 $< $(WAVETABLE_LENGTH) $(SAMPLE_RATE) > $@ || (rm $@ && false)

clean:
	rm -rf build termbox/.waf*
