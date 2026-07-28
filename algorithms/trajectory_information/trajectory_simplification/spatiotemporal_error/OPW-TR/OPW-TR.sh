#!/usr/bin/env bash
set -euo pipefail
g++ OPW_TR.cpp -o OPW-TR
./OPW-TR sample_input.txt 0.0003 sample_output.txt
