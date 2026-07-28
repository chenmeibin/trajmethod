#!/usr/bin/env bash
set -euo pipefail
g++ -std=c++17 -O2 VOLTComTest.cpp -o VOLTComTest
./VOLTComTest sample_input.txt 0.00001 sample_output.txt 20
