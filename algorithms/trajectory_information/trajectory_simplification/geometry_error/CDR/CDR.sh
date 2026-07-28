#!/usr/bin/env bash
set -euo pipefail
g++ -std=c++17 -O2 CDR.cpp -o CDR
./CDR CDR sample_input.txt 100 sample_output.txt 35
