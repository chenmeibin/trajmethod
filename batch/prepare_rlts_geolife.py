from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path


HEADER_LINES = 6


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert Geolife .plt trajectories into RLTS Geolife_out numbered trajectory files."
    )
    parser.add_argument("--input-dir", default=r"D:\all_traj_method\data\Geolife")
    parser.add_argument("--output-dir", default=r"D:\all_traj_method\RLTS\TrajData\Geolife_out")
    parser.add_argument("--min-points", type=int, default=31, help="Minimum points to keep a trajectory file")
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
            if len(parts) < 7:
                continue
            lat = float(parts[0])
            lon = float(parts[1])
            ts = int(datetime.strptime(parts[5].strip() + " " + parts[6].strip(), "%Y-%m-%d %H:%M:%S").timestamp())
            yield lat, lon, ts


def main() -> None:
    args = parse_args()
    input_dir = Path(args.input_dir).resolve()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    for old in output_dir.iterdir():
        if old.is_file() and old.name.isdigit():
            old.unlink()

    kept = 0
    skipped = 0
    total_points = 0
    source_files = sorted(input_dir.rglob("*.plt"))

    for plt_path in source_files:
        points = list(iter_points(plt_path))
        if len(points) < args.min_points:
            skipped += 1
            continue

        out_path = output_dir / str(kept)
        with out_path.open("w", encoding="utf-8", newline="\n") as out:
            for lat, lon, ts in points:
                out.write(f"{lat} {lon} {ts}\n")
        total_points += len(points)
        kept += 1

    summary_path = output_dir / "prepare_summary.txt"
    summary_path.write_text(
        "\n".join(
            [
                f"source_files={len(source_files)}",
                f"kept_trajectories={kept}",
                f"skipped_trajectories={skipped}",
                f"total_points={total_points}",
                f"min_points={args.min_points}",
            ]
        ),
        encoding="utf-8",
    )

    print(f"source_files={len(source_files)}")
    print(f"kept_trajectories={kept}")
    print(f"skipped_trajectories={skipped}")
    print(f"total_points={total_points}")
    print(f"summary={summary_path}")


if __name__ == "__main__":
    main()
