#!/usr/bin/env bash
set -euo pipefail

# MARL4TS uses the Geolife directory layout documented in README.md.
# Run from this directory after restoring TrajData from the external assets archive.
python3 preprocess.py
python3 rl_index_run_multi_agent_with_constraint.py
