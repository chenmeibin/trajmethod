#!/usr/bin/env bash
set -e

g++ main.cpp -O2 -std=c++17 -o OPERB_cpp
./OPERB_cpp sample_input.txt 0.0002 sample_output.txt
