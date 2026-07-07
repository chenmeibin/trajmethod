# MRPA C++ Port

This directory contains a standalone C++ port of the core Matlab `TDMRPA_SED` pipeline from [`batch/MRPA`](D:/all_traj_method/batch/MRPA).

Current scope:
- Reads raw `Geolife .plt` trajectories
- Projects latitude/longitude with the same Mercator-style transform used in `mercator_proj.m`
- Runs the core `TDMRPA_SED` simplification flow
- Writes simplified `lat lon unix_time` points plus runtime to an output file

Notes:
- This is kept separate from the original Matlab sources on purpose.
- The threshold is the `LISSEDth` parameter from the Matlab version.
- The current executable is focused on the Geolife-style raw input first.

Build:

```powershell
g++ -O2 -std=c++17 .\batch\MRPA_cpp_port\main.cpp -o .\batch\MRPA_cpp_port\mrpa_cpp.exe
```

Usage:

```powershell
.\batch\MRPA_cpp_port\mrpa_cpp.exe <input_geolife_plt> <lissed_threshold> <output_file>
```

Output format:
- Simplified points: `lat lon unix_time`
- Final line: runtime in seconds
