#!/usr/bin/env bash
set -euo pipefail
g++ -std=c++17 -O2 main.cpp -o DOTS_cpp
./DOTS_cpp sample_input.txt 0.0003 sample_output.txt
