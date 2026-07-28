#!/usr/bin/env bash
set -e

g++ main.cpp -O2 -std=c++17 -o Persistence_cpp
./Persistence_cpp sample_input.txt 10 sample_output.txt
