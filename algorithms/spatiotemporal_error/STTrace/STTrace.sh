#!/usr/bin/env bash
set -euo pipefail
g++ -std=c++11 STTrace.cpp -o STTrace
./STTrace sample_input.txt 0.2 sample_output.txt

