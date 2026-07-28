#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
mkdir -p build
g++ -std=c++17 -O2 ciseds_reference.cpp -o build/ciseds_reference
./build/ciseds_reference sample_input.txt 1.0 8 sample_output.txt | tee run_metrics.txt
