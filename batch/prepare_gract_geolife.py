from __future__ import annotations

import argparse
from datetime import datetime
from pathlib import Path


HEADER_LINES = 6
COORD_SCALE = 1_000_000


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert Geolife .plt files into GraCT's `id t x y` format."
    )
    parser.add_argument("--input-dir", required=True, help="Directory containing Geolife .plt files")
    parser.add_argument("--output", required=True, help="Output text file for GraCT")
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
            dt = datetime.strptime(f"{date_str} {time_str}", "%Y-%m-%d %H:%M:%S")
            yield lat, lon, dt


def main() -> None:
    args = parse_args()
    input_dir = Path(args.input_dir)
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    files = sorted(input_dir.rglob("*.plt"))
    total_points = 0

    with output_path.open("w", encoding="utf-8", newline="\n") as out:
        for obj_id, plt_path in enumerate(files):
            start_dt = None
            prev_t = 0
            wrote_any = False
            for lat, lon, dt in iter_points(plt_path):
                if start_dt is None:
                    start_dt = dt
                t = int(round((dt - start_dt).total_seconds())) + 1
                if t <= prev_t:
                    t = prev_t + 1
                x = round(lon * COORD_SCALE)
                y = round(lat * COORD_SCALE)
                out.write(f"{obj_id} {t} {x} {y}\n")
                prev_t = t
                total_points += 1
                wrote_any = True
            if not wrote_any:
                # Preserve contiguous ids by writing nothing for empty files.
                continue

    print(f"files={len(files)}")
    print(f"points={total_points}")
    print(f"coord_scale={COORD_SCALE}")
    print(f"output={output_path}")


if __name__ == "__main__":
    main()
