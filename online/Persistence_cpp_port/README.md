# Persistence C++ Port

This directory contains a standalone local C++ port of the repository's `PersistenceBatchSimplifier` batch core.

Scope:
- Ports the `GeoLife -> mercatorProject -> normalizeData -> simplifyByIndexCascade` path.
- Keeps the repository's cascade-style persistence simplification behavior.
- Excludes Qt UI and comparison tooling.

Notes:
- The exposed parameter is `minimum_distance`, not a raw latitude/longitude epsilon.
- To stay faithful to the original repository logic, this port reads raw GeoLife `.plt` files directly and uses Mercator projection plus mean normalization before simplification.
