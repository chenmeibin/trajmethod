# DOTS C++ Port

This directory contains a standalone local C++ port of the repository's Qt-based `online/DOTS` batch core.

Scope:
- Ports the `GeoLife -> mercatorProject -> normalizeData -> batchDotsByIndex` path.
- Focuses on standard DOTS batch simplification only.
- Excludes Qt UI, plotting, and cascade mode.

Notes:
- The exposed parameter is `LSSD threshold`, not a raw latitude/longitude epsilon.
- To stay faithful to the original repository logic, this port reads raw GeoLife `.plt` files directly and uses the same `parts[4]` timestamp field plus Mercator projection/normalization.
- Treat this as a local executable port for benchmarking until we finish deeper side-by-side validation.
