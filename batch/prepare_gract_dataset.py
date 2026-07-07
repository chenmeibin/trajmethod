from __future__ import annotations

import argparse
import json
from datetime import datetime
from pathlib import Path


GEOLIFE_HEADER_LINES = 6
COORD_SCALE = 1_000_000


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert trajectory datasets into GraCT `id t x y` format."
    )
    parser.add_argument(
        "--dataset",
        required=True,
        choices=["chengdu", "Geolife", "T-drive", "wuxi"],
        help="Dataset name.",
    )
    parser.add_argument("--input-dir", required=True, help="Dataset directory.")
    parser.add_argument("--output", required=True, help="Output GraCT text file.")
    return parser.parse_args()


def parse_datetime(value: str) -> datetime:
    value = value.strip()
    for fmt in ("%Y-%m-%d %H:%M:%S.%f", "%Y-%m-%d %H:%M:%S"):
        try:
            return datetime.strptime(value, fmt)
        except ValueError:
            continue
    raise ValueError(f"Unsupported datetime: {value}")


def iter_geolife_points(path: Path):
    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        for _ in range(GEOLIFE_HEADER_LINES):
            next(handle, None)
        for line in handle:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) < 7:
                continue
            try:
                lat = float(parts[0])
                lon = float(parts[1])
                dt = parse_datetime(f"{parts[5].strip()} {parts[6].strip()}")
            except ValueError:
                continue
            yield lon, lat, dt


def iter_tdrive_points(path: Path):
    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        for line in handle:
            line = line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) < 4:
                continue
            try:
                dt = parse_datetime(parts[1])
                lon = float(parts[2])
                lat = float(parts[3])
            except ValueError:
                continue
            yield lon, lat, dt


def iter_simple_points(path: Path):
    with path.open("r", encoding="utf-8", errors="ignore") as handle:
        first_line = True
        for line in handle:
            line = line.strip()
            if not line:
                continue
            if first_line:
                first_line = False
                continue
            parts = line.split(",")
            if len(parts) < 3:
                continue
            try:
                dt = parse_datetime(parts[0])
                lon = float(parts[1])
                lat = float(parts[2])
            except ValueError:
                continue
            yield lon, lat, dt


def iter_dataset_files(dataset: str, input_dir: Path):
    if dataset == "Geolife":
        return sorted(input_dir.rglob("*.plt"))
    return sorted(
        path for path in input_dir.rglob("*.txt") if path.stem.isdigit()
    )


def point_iterator(dataset: str, path: Path):
    if dataset == "Geolife":
        return iter_geolife_points(path)
    if dataset == "T-drive":
        return iter_tdrive_points(path)
    return iter_simple_points(path)


def main() -> None:
    args = parse_args()
    input_dir = Path(args.input_dir)
    output_path = Path(args.output)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    files = iter_dataset_files(args.dataset, input_dir)
    total_points = 0
    total_bytes = 0
    trajectory_count = 0

    with output_path.open("w", encoding="utf-8", newline="\n") as out:
        for obj_id, source_path in enumerate(files):
            total_bytes += source_path.stat().st_size
            start_dt = None
            prev_t = 0
            wrote_any = False
            for lon, lat, dt in point_iterator(args.dataset, source_path):
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
            if wrote_any:
                trajectory_count += 1

    stats = {
        "dataset": args.dataset,
        "input_dir": str(input_dir),
        "output": str(output_path),
        "trajectory_count": trajectory_count,
        "point_count": total_points,
        "raw_source_bytes": total_bytes,
        "coord_scale": COORD_SCALE,
    }
    print(json.dumps(stats, ensure_ascii=True))


if __name__ == "__main__":
    main()
