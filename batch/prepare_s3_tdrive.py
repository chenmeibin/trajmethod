from __future__ import annotations

import argparse
import json
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert T-drive trajectories into S3's one-line-per-trajectory text format."
    )
    parser.add_argument("--input-dir", required=True, help="Directory containing T-drive txt files")
    parser.add_argument("--output-dir", required=True, help="Output directory, typically S3/datasets/tdrive")
    parser.add_argument(
        "--output-name",
        default="tdrive.pos",
        help="Name of the generated trajectory text file",
    )
    return parser.parse_args()


def iter_points(txt_path: Path):
    with txt_path.open("r", encoding="utf-8", errors="ignore") as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) < 4:
                continue
            lon = float(parts[2])
            lat = float(parts[3])
            yield [lon, lat]


def main() -> None:
    args = parse_args()
    input_dir = Path(args.input_dir).resolve()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    output_path = output_dir / args.output_name
    files = sorted(path for path in input_dir.glob("*.txt") if path.stem.isdigit())

    total_points = 0
    kept_trajs = 0
    skipped_empty = 0
    min_points = None
    max_points = 0

    with output_path.open("w", encoding="utf-8", newline="\n") as out:
        for txt_path in files:
            points = list(iter_points(txt_path))
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

    metadata = {
        "algorithm": "S3",
        "dataset": "T-drive",
        "input_dir": str(input_dir),
        "output_file": str(output_path),
        "source_file_count": len(files),
        "written_trajectories": kept_trajs,
        "skipped_empty_files": skipped_empty,
        "total_points": total_points,
        "min_points_per_trajectory": min_points or 0,
        "max_points_per_trajectory": max_points,
        "format_note": "Each line is one trajectory, written as a JSON array of [lon, lat] points.",
    }
    (output_dir / "prepare_summary.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")

    print(f"source_file_count={len(files)}")
    print(f"written_trajectories={kept_trajs}")
    print(f"skipped_empty_files={skipped_empty}")
    print(f"total_points={total_points}")
    print(f"output_file={output_path}")


if __name__ == "__main__":
    main()
