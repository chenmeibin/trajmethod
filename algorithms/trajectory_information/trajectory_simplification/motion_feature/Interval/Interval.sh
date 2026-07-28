#!/usr/bin/env bash
set -e

g++ main.cpp -O2 -std=c++17 -o Interval_cpp
./Interval_cpp sample_input.txt 0.1 sample_output.txt
