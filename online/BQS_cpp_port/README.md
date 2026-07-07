# BQS C++ Port

This directory contains a standalone C++ port of the core online `BQS` path used by `online/BQS/amnesic_bubbling.py`.

Scope:
- Ports the main `test_bqs -> run_one -> TrajProcessor/QuadrantSystem` compression flow.
- Accepts whitespace-separated input where the first two columns are `x y`.
- Writes retained input lines followed by the runtime in seconds on the last line, matching the repository's other online executables.

Notes:
- The parameter is the same raw coordinate-space epsilon used by the original Python code.
- This port intentionally excludes plotting and the amnesic-buffer experiment code around `ABQS/FBQS`.
- The implementation is based on the repository's Python logic and should still be treated as a local port until we finish side-by-side output validation on sample trajectories.

Build example:

```powershell
g++ .\online\BQS_cpp_port\main.cpp -O2 -std=c++17 -o .\online\BQS_cpp_port\BQS_cpp.exe
```

Run example:

```powershell
.\online\BQS_cpp_port\BQS_cpp.exe .\online\BQS\20081023025304-0.plt 0.00025 .\tmp\bqs_out.txt
```
