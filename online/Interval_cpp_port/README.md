# Interval C++ Port

This directory contains a standalone local C++ port of the repository's Java `online/Interval` implementation.

Scope:
- Ports the core interval-intersection simplification logic.
- Accepts whitespace-separated `x y t` input.
- Writes retained lines followed by runtime seconds on the last line.

Notes:
- The exposed parameter is the same angular interval error used by the original Java implementation.
- This is a local executable port for benchmarking consistency.
