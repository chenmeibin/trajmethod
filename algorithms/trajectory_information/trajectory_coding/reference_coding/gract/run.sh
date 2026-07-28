#!/usr/bin/env bash
set -euo pipefail
cmake -S . -B build
cmake --build build
echo "GraCT built. See README.md for GraCT_create_index and GraCT_run_queries usage."
