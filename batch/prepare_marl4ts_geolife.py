from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path


HEADER_LINES = 6


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert Geolife .plt trajectories into MARL4TS Geolife_out numbered trajectory files."
    )
    parser.add_argument("--input-dir", default=r"D:\all_traj_method\data\Geolife")
    parser.add_argument("--output-dir", default=r"D:\all_traj_method\MARL4TS\TrajData\Geolife_out")
    parser.add_argument("--min-points", type=int, default=31)
    return parser.parse_args()


def iter_points(plt_path: Path):
    with plt_path.open("r", encoding="utf-8", errors="ignore") as f:
        for _ in range(HEADER_LINES):
            next(f, None)
        for line in f:
            parts = line.strip().split(",")
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
    for plt_path in sorted(input_dir.rglob("*.plt")):
        points = list(iter_points(plt_path))
        if len(points) < args.min_points:
            skipped += 1
            continue
        out_path = output_dir / str(kept)
        with out_path.open("w", encoding="utf-8", newline="\n") as out:
            for lat, lon, ts in points:
                out.write(f"{lat} {lon} {ts}\n")
        kept += 1
        total_points += len(points)

    summary_path = output_dir / "prepare_summary.txt"
    summary_path.write_text(
        "\n".join(
            [
                f"kept_trajectories={kept}",
                f"skipped_trajectories={skipped}",
                f"total_points={total_points}",
                f"min_points={args.min_points}",
            ]
        ),
        encoding="utf-8",
    )
    print(f"kept_trajectories={kept}")
    print(f"skipped_trajectories={skipped}")
    print(f"total_points={total_points}")
    print(f"summary={summary_path}")


if __name__ == "__main__":
    main()
