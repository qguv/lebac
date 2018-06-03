#!/usr/bin/env python3

from math import sin, pi
from sys import argv, exit, stderr

def mksintable(length):
    print("#include <stdint.h>\n")
    print(f"static int16_t sin_table[{length}] =", "{")
    if length > 0:
        print("    0", end="")
    if length > 1:
        half_max_int = 1 << 14
        for i in range(1, length):
            el = int(sin(((2 * pi) / length) * i) * half_max_int)
            print(f",\n    {el}", end="")
    print("\n};")

if len(argv) < 2:
    print("Usage:", argv[0], "SINTABLE_LENGTH", file=stderr)
    exit(1)
else:
    mksintable(int(argv[1]))
