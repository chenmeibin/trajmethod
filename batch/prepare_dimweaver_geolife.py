from __future__ import annotations

import argparse
from datetime import datetime, timezone
from pathlib import Path


HEADER_LINES = 6


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert Geolife .plt trajectories into DimWeaver CSV files."
    )
    parser.add_argument("--input-dir", required=True, help="Directory containing Geolife .plt files")
    parser.add_argument("--output-dir", required=True, help="Directory for converted CSV files")
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
            date_str = parts[5].strip()
            time_str = parts[6].strip()
            dt = datetime.strptime(f"{date_str} {time_str}", "%Y-%m-%d %H:%M:%S").replace(tzinfo=timezone.utc)
            yield dt, lon, lat


def main() -> None:
    args = parse_args()
    input_dir = Path(args.input_dir)
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    files = sorted(input_dir.rglob("*.plt"))
    converted = 0
    total_points = 0

    for plt_path in files:
        out_path = output_dir / f"{plt_path.stem}.csv"
        points = list(iter_points(plt_path))
        if not points:
            continue
        with out_path.open("w", encoding="utf-8", newline="\n") as out:
            out.write("t,x,y\n")
            prev_t = None
            for dt, lon, lat in points:
                t = int(dt.timestamp())
                if prev_t is not None and t <= prev_t:
                    t = prev_t + 1
                out.write(f"{t},{lon:.15f},{lat:.15f}\n")
                prev_t = t
                total_points += 1
        converted += 1

    print(f"files={converted}")
    print(f"points={total_points}")
    print(f"output_dir={output_dir}")


if __name__ == "__main__":
    main()
