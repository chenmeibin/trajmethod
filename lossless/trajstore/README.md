TrajStore C++ Reference
=======================

This directory contains a standalone C++ implementation of the clustering
logic used by the TrajStore paper and the existing
`lossless/trajic/clustering/trajstoreClustering.scala` script.

What it does
------------

* Splits points into spatial grid cells.
* Groups points by `(grid_cell, trajectory_id)`.
* Greedily chooses cluster-center trajectories inside each cell.
* Assigns similar trajectories as members if their max aligned spatial
  deviation is below the configured threshold.
* Stores member trajectories as `(center_id, member_id, time_sequence)`.

Build
-----

```sh
make
```

Usage
-----

```sh
trajstore \
  --input-dir /path/to/trajs \
  --output-dir /path/to/output \
  --max-error 0.03 \
  --lat-divs 20 \
  --lon-divs 20 \
  --format time_lat_lon
```

Input
-----

Each input file is treated as one trajectory. Every non-empty line must contain
three numeric columns separated by commas or whitespace.

Supported formats:

* `time_lat_lon`
* `lat_lon_time`

Output
------

* `centerList-maxError-<err>.txt`
* `memberList-maxError-<err>.txt`
* `compressionRatio.txt`

