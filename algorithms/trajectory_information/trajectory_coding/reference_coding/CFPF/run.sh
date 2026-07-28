#!/usr/bin/env bash
set -euo pipefail
mingw32-make
echo "Built bin/cfpf_cli.exe and bin/cfpf_codec.exe. See README.md for required SEB inputs."
