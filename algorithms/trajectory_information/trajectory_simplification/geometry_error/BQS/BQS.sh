#!/usr/bin/env bash
set -e

g++ main.cpp -O2 -std=c++17 -o BQS_cpp
./BQS_cpp sample_input.txt 0.15 sample_output.txt
