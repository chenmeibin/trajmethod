#!/usr/bin/env bash
set -euo pipefail
python3 preprocess.py
echo "Preprocessing complete. Run the batch-rlts or online-rlts training entry described in README.md."
