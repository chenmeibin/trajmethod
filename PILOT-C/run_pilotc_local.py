import argparse
import csv
import json
import os
from pathlib import Path

from no_map_compress import process_file


def parse_args():
    parser = argparse.ArgumentParser(
        description="Run PILOT-C and baseline compressors on a local trajectory file or folder."
    )
    parser.add_argument(
        "--input",
        required=True,
        help="Trajectory file for geolife/mopsi/test_scalability, or a folder containing ego.csv for nuplan.",
    )
    parser.add_argument(
        "--data-source",
        choices=["geolife", "mopsi", "test_scalability", "nuplan", "geolife_3d"],
        default="test_scalability",
    )
    parser.add_argument("--max-error", type=float, default=None)
    parser.add_argument("--utf", type=int, default=None)
    parser.add_argument("--accuracy", type=float, default=None)
    parser.add_argument("--block-size", type=int, default=None)
    parser.add_argument("--scale-length", type=float, default=None)
    parser.add_argument("--epsilon", type=float, default=None)
    parser.add_argument(
        "--output",
        default=None,
        help="Optional CSV path for metric output. Defaults to workspace/result/<data-source>/<input-name>/metrics.csv.",
    )
    return parser.parse_args()


def build_default_output(input_path: Path, data_source: str) -> Path:
    project_root = Path(__file__).resolve().parent
    result_root = Path(os.environ.get("PRESS_RESULT_ROOT", project_root / "workspace" / "result"))
    return result_root / data_source / input_path.stem / "metrics.csv"


def main():
    args = parse_args()
    input_path = Path(args.input).resolve()
    if not input_path.exists():
        raise FileNotFoundError(f"Input not found: {input_path}")

    results, logs = process_file(
        str(input_path),
        args.data_source,
        args.max_error,
        args.utf,
        args.accuracy,
        args.block_size,
        args.scale_length,
        args.epsilon,
    )

    rows = []
    for method, metrics in results:
        rows.append({"method": method, **metrics})

    output_path = Path(args.output) if args.output else build_default_output(input_path, args.data_source)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    with output_path.open("w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(
            f,
            fieldnames=["method", "max_error", "mean_error", "compress_ratio", "length", "error_cnt", "var_error", "time"],
        )
        writer.writeheader()
        writer.writerows(rows)

    print(json.dumps(rows, ensure_ascii=False, indent=2))
    print(f"\nSaved metrics to: {output_path}")
    print("\nRun log:\n")
    print(logs)


if __name__ == "__main__":
    main()
