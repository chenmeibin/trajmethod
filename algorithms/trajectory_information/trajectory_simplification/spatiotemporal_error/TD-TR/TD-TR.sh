#!/usr/bin/env bash
set -euo pipefail
g++ TD-TR.cpp -o TD-TR
./TD-TR sample_input.txt 0.0065 sample_output.txt
