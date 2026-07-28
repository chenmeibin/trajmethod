#!/usr/bin/env bash
set -euo pipefail
g++ -std=c++11 SQUISH.cpp -o SQUISH
./SQUISH sample_input.txt 0.2 sample_output.txt
