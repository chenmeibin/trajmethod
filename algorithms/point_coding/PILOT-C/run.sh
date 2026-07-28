#!/usr/bin/env bash
set -euo pipefail
cd cpp_port
mkdir -p build
g++ -std=c++17 -O2 src/pilotc_reference.cpp -o build/pilotc_reference
cd ..
./cpp_port/build/pilotc_reference --input sample_input.txt --data-source test_scalability --max-error 1.0 | tee sample_output.txt
