#!/bin/bash
mkdir -p songs && make && build/lebac "songs/$(date +%s).bac"
