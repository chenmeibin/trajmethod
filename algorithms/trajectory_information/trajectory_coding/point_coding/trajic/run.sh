#!/usr/bin/env bash
set -euo pipefail
make
echo "Trajic built. Compress with: ./trajic c sample_input.txt"
