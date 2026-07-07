param(
    [string]$WorkspaceRoot = "D:\all_traj_method"
)

$ErrorActionPreference = "Stop"

$python = Join-Path $WorkspaceRoot "MLSimp\.venv\Scripts\python.exe"
$script = Join-Path $WorkspaceRoot "batch\prepare_s3_geolife_dataset.py"

& $python $script `
    --workspace-root $WorkspaceRoot `
    --pos-file "S3/datasets/geolife/geolife.pos" `
    --min-length 30 `
    --max-length 400 `
    --train-ratio 0.60 `
    --val-ratio 0.20 `
    --eval-ratio 0.20
