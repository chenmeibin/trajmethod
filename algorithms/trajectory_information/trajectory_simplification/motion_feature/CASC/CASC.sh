#!/usr/bin/env bash
set -e

g++ main.cpp -O2 -std=c++17 -o CASC_cpp
./CASC_cpp sample_input.txt 0.55850536 1.0 10 sample_output.txt
