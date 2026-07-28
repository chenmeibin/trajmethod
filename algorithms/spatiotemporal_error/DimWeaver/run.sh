#!/usr/bin/env bash
set -euo pipefail
mkdir -p cpp/build
g++ -std=c++17 -O2 -Icpp/include cpp/src/dimweaver.cpp cpp/src/main.cpp -o cpp/build/dimweaver_cli
./cpp/build/dimweaver_cli sample_input.txt 0.0001 0.85 | tee sample_output.txt
