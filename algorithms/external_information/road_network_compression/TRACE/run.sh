#!/usr/bin/env bash
set -euo pipefail

root="${PRESS3_ROOT:-D:\\PRESS3}"
out="${TRACE_OUTPUT_DIR:-$root/trace/output/repo_sample}"
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
  "& '$root/bin/trace_runner.exe' --root '$root' --dataset chengdu --gps-file 1.txt --output-dir '$out' --use-existing-binaries"
