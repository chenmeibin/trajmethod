#!/usr/bin/env bash
set -e

g++ -g ./src/*.cpp -o ./DPTS
./DPTS
mv -f stat.txt sample_output.txt
