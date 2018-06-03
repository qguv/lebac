#!/bin/bash
songfile="${1:-songs/$(date +%s).bac}"
mkdir -p songs && make && build/lebac "$songfile"
