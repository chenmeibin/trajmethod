#!/usr/bin/env bash
set -euo pipefail
make hull
./hull sample_input.txt 908 0.00005 sample_output.txt
