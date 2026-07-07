param(
    [string]$WorkspaceRoot = "D:\all_traj_method"
)

$ErrorActionPreference = "Stop"

$python = "python"
$script = Join-Path $WorkspaceRoot "batch\prepare_s3_geolife.py"
$inputDir = Join-Path $WorkspaceRoot "data\Geolife"
$outputDir = Join-Path $WorkspaceRoot "S3\datasets\geolife"

& $python $script `
    --input-dir $inputDir `
    --output-dir $outputDir `
    --output-name "geolife.pos"
