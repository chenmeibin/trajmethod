# PILOT-C ABC Tuning Plan

This tuning flow fixes one global `a,b,c` per dataset and evaluates that same triple across all chosen thresholds.

Important: tuning and final comparison happen within the same dataset. We do not tune on one dataset and test on another dataset.

## Selection Rule

1. For each dataset, prepare a tuning subset sampled from that same dataset.
2. Keep the final horizontal comparison dataset identical to the one used by the other algorithms.
3. Use the tuning subset only to choose one dataset-wide `a,b,c`.
4. After `a,b,c` is fixed, rerun PILOT-C on the full comparison dataset for that dataset.
5. For each candidate `a,b,c`, run all selected thresholds on the same tuning files.
6. A candidate is valid only if every tested threshold satisfies the overflow constraint:
   - `overflow_ratio = error_points / total_points`
   - default requirement: `overflow_ratio = 0`
7. Among valid candidates, choose the one with the highest average `compression ratio`.
8. If multiple candidates are very close, prefer the one with lower average mean error.

No weighted composite score is used.

## Example

```powershell
cd D:\all_traj_method\PILOT-C\cpp_port
powershell -ExecutionPolicy Bypass -File .\build.ps1

powershell -ExecutionPolicy Bypass -File .\tune_pilotc_abc.ps1 `
  -InputDir D:\all_traj_method\data\T-drive `
  -InputFormat tdrive `
  -DataSource geolife `
  -ThresholdMode degree `
  -Thresholds "1e-4,5e-5,1e-5" `
  -AValues "0.5,1,2" `
  -BValues "25,50,100" `
  -CValues "0.6,0.8,1.1" `
  -Accuracy 0.5 `
  -AllowedOverflowRatio 0 `
  -MaxFiles 50 `
  -ResultDir D:\all_traj_method\PILOT-C\cpp_port\build\records\t_drive_abc_search
```

## Outputs

- `tune_results.csv`
  One row per `(a,b,c,threshold)` summary.
- `tune_best.csv`
  One row per `(a,b,c)` aggregated across all thresholds, already ranked.
- `search_config.txt`
  Records the exact search space and dataset subset used in that run.

## Notes

- For degree-based datasets such as raw T-drive, use `-InputFormat tdrive` and `-ThresholdMode degree`.
- The script compares candidates using the same threshold set and the same file subset.
- Final horizontal comparison with other algorithms should use the selected dataset-wide `a,b,c` without further changes.
- The final comparison should be rerun on the full benchmark dataset used by the other algorithms, not only on the tuning subset.
- Recommended directory layout:
  - `build\records\<dataset>_abc_search\` for the full parameter search trail
  - `build\records\<dataset>_<threshold>\` for final full-dataset runs at a specific threshold
