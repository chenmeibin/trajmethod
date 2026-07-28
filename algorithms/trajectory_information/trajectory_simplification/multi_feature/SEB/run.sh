#!/usr/bin/env bash
set -euo pipefail
mkdir -p bin
g++ -std=c++17 -O3 -Wall -Wextra -pedantic \
  src/main.cpp src/preprocessing.cpp src/seb_algorithm.cpp \
  -o bin/seb_cli.exe
tmp_dir="$(mktemp -d)"
trap 'rm -rf "$tmp_dir"' EXIT
cp sample_input.txt "$tmp_dir/sample.txt"
mkdir -p "$tmp_dir/output"
./bin/seb_cli.exe --input-dir "$tmp_dir" --output-dir "$tmp_dir/output" \
  --budget 0.5 --min-trajectory-points 3 --include-short-trajectories
cp "$tmp_dir/output/compressed_points.csv" sample_output.txt
echo "SEB sample complete: sample_output.txt"
