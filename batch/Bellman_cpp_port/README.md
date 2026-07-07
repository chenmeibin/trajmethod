# Bellman Dynamic Programming Port

This directory contains a trajectory-oriented reproduction of the Bellman dynamic-programming idea from:

- Richard Bellman, "On the Approximation of Curves by Line Segments Using Dynamic Programming"

Scope:

- Input is an ordered polyline represented as whitespace-separated rows.
- The executable accepts `input_file`, `num_kept_points`, and `output_file`.
- It keeps exactly `M` points, including the first and last input point.
- It minimizes RMS error, implemented via an equivalent dynamic-programming minimization of SSE.
- Output contains the retained original points followed by metadata lines for runtime and RMSE.

Notes:

- The original Bellman note is written for piecewise-linear approximation with a fixed number of pieces and a global dynamic-programming recurrence. This port adapts that principle to discrete trajectory simplification with a fixed number of retained points.
- This is a retained-point-count-constrained offline algorithm, not an epsilon-constrained one.
- Exact dynamic programming is expensive. The accompanying batch script defaults to a point-count-limited subset for practical runs.

Build example:

```powershell
g++ .\batch\Bellman_cpp_port\main.cpp -O2 -std=c++17 -o .\batch\Bellman_cpp_port\bellman.exe
```

Run example:

```powershell
.\batch\Bellman_cpp_port\bellman.exe .\tmp\sample.txt 16 .\tmp\bellman_out.txt
```
