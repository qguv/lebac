#!/bin/bash
gcc -Wall -Wpedantic -Wextra -Werror -lmpg123 -lout123 -o /tmp/bac main.c && /tmp/bac "$@" && rm /tmp/bac
