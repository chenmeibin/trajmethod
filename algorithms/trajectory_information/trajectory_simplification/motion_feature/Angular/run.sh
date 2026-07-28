#!/usr/bin/env bash
set -e

g++ Angular.cpp -O2 -std=c++17 -o Angular_cpp
./Angular_cpp sample_input.txt 0.2 sample_output.txt
