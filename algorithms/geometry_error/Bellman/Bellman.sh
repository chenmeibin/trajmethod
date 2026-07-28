#!/usr/bin/env bash
set -e

g++ main.cpp -O2 -std=c++17 -o bellman
./bellman sample_input.txt 2 sample_output.txt
