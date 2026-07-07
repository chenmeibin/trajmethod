# DimWeaver C++

This directory contains a C++ port of the core DimWeaver trajectory compression workflow.

## Included

- Core trajectory compression logic
- Segment grouping
- Binary encode/decode for the C++ port
- Trajectory reconstruction
- CSV reader for `t,x,y` input
- Simple CLI for end-to-end validation

## Build

If `cmake` is available:

```bash
cmake -S . -B build
cmake --build build
```

If you only have `g++`:

```bash
g++ -std=c++17 -Iinclude src/dimweaver.cpp src/main.cpp -o build/dimweaver_cli
```

## Run

```bash
build/dimweaver_cli <trajectory.csv> <epsilon> <c>
```

Example:

```bash
build/dimweaver_cli ../../src/test/resources/geolife/000-1.csv 0.0001 0.85
```

## Note

The C++ version preserves the algorithmic workflow and end-to-end compression/decompression behavior, but its binary packing is implemented independently in C++ and is not intended as a drop-in binary compatibility layer for the Java `JavaFastPFOR` path.
