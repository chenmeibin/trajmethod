from __future__ import annotations

import argparse
import json
from pathlib import Path


HEADER_LINES = 6


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert Geolife .plt trajectories into S3's one-line-per-trajectory text format."
    )
    parser.add_argument("--input-dir", required=True, help="Directory containing Geolife .plt files")
    parser.add_argument("--output-dir", required=True, help="Output directory, typically S3/datasets/geolife")
    parser.add_argument(
        "--output-name",
        default="geolife.pos",
        help="Name of the generated trajectory text file",
    )
    return parser.parse_args()


def iter_points(plt_path: Path):
    with plt_path.open("r", encoding="utf-8", errors="ignore") as f:
        for _ in range(HEADER_LINES):
            next(f, None)
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) < 2:
                continue
            lat = float(parts[0])
            lon = float(parts[1])
            yield [lon, lat]


def main() -> None:
    args = parse_args()
    input_dir = Path(args.input_dir).resolve()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    output_path = output_dir / args.output_name
    files = sorted(input_dir.rglob("*.plt"))

    total_points = 0
    kept_trajs = 0
    skipped_empty = 0
    min_points = None
    max_points = 0

    with output_path.open("w", encoding="utf-8", newline="\n") as out:
        for plt_path in files:
            points = list(iter_points(plt_path))
            if not points:
                skipped_empty += 1
                continue

            point_count = len(points)
            total_points += point_count
            kept_trajs += 1
            min_points = point_count if min_points is None else min(min_points, point_count)
            max_points = max(max_points, point_count)
            out.write(json.dumps(points, ensure_ascii=True))
            out.write("\n")

    metadata_path = output_dir / "prepare_summary.json"
    metadata = {
        "algorithm": "S3",
        "dataset": "Geolife",
        "input_dir": str(input_dir),
        "output_file": str(output_path),
        "source_file_count": len(files),
        "written_trajectories": kept_trajs,
        "skipped_empty_files": skipped_empty,
        "total_points": total_points,
        "min_points_per_trajectory": min_points or 0,
        "max_points_per_trajectory": max_points,
        "format_note": "Each line is one trajectory, written as a JSON array of [lon, lat] points.",
        "next_step_defaults": {
            "ntrain": 3000,
            "nval": 1000,
            "neval": 1000,
            "min_length": 30,
            "max_length": 400,
        },
    }
    metadata_path.write_text(json.dumps(metadata, indent=2), encoding="utf-8")

    print(f"source_file_count={len(files)}")
    print(f"written_trajectories={kept_trajs}")
    print(f"skipped_empty_files={skipped_empty}")
    print(f"total_points={total_points}")
    print(f"output_file={output_path}")
    print(f"summary_file={metadata_path}")


if __name__ == "__main__":
    main()
