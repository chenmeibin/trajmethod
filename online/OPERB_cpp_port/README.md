# OPERB C++ Port

This directory contains a standalone C++ port of the repository's Java `online/OPERB` implementation.

Scope:
- Ports the main `getNextValidPoint + compressByOPERA` logic.
- Accepts whitespace-separated input where the first three columns are `lat lon time`.
- Writes retained lines followed by runtime seconds on the last line.

Notes:
- Although the Java comments mention PED error, the repository implementation actually uses plain Euclidean distance over raw latitude/longitude coordinates.
- This port follows that repository behavior exactly for testing consistency.
- Treat this as a local C++ port until we finish more side-by-side validation on sample trajectories.

Build example:

```powershell
g++ .\online\OPERB_cpp_port\main.cpp -O2 -std=c++17 -o .\online\OPERB_cpp_port\OPERB_cpp.exe
```
