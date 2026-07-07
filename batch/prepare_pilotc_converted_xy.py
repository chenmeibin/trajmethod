from __future__ import annotations

import argparse
import math
from pathlib import Path

from pyproj import CRS, Transformer


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert simple whitespace-separated lat lon time trajectories into projected PILOT-C-style t,x,y files."
    )
    parser.add_argument("--input-dir", required=True, help="Directory containing input .plt files.")
    parser.add_argument("--output-dir", required=True, help="Directory to write converted txt files.")
    return parser.parse_args()


def pick_utm_zone(lon: float) -> int:
    return max(1, min(60, int(math.floor((lon + 180.0) / 6.0)) + 1))


def load_points(src: Path):
    points: list[tuple[int, float, float]] = []
    with src.open("r", encoding="utf-8", errors="ignore") as fh:
        for raw_line in fh:
            line = raw_line.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) < 3:
                continue
            try:
                lat = float(parts[0])
                lon = float(parts[1])
                ts = int(float(parts[2]))
            except ValueError:
                continue
            points.append((ts, lon, lat))
    return points


def convert_file(src: Path, dst: Path) -> int:
    points = load_points(src)
    if len(points) < 2:
        return 0

    first_lon = points[0][1]
    first_lat = points[0][2]
    zone = pick_utm_zone(first_lon)
    target_crs = CRS.from_dict({"proj": "utm", "zone": zone, "south": first_lat < 0})
    transformer = Transformer.from_crs("EPSG:4326", target_crs, always_xy=True)

    lines: list[str] = []
    last_t: int | None = None
    for ts, lon, lat in points:
        if last_t is not None and ts == last_t:
            continue
        if last_t is not None and ts < last_t:
            ts = last_t + 1
        x, y = transformer.transform(lon, lat)
        lines.append(f"{ts},{x},{y}")
        last_t = ts

    if len(lines) < 2:
        return 0

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text("\n".join(lines), encoding="utf-8")
    return len(lines)


def main() -> None:
    args = parse_args()
    input_dir = Path(args.input_dir).resolve()
    output_dir = Path(args.output_dir).resolve()

    if not input_dir.exists():
        raise FileNotFoundError(f"Input dir not found: {input_dir}")

    count = 0
    converted = 0
    for src in sorted(input_dir.rglob("*.plt")):
        rel = src.relative_to(input_dir)
        dst = output_dir / rel.with_suffix(".txt")
        points = convert_file(src, dst)
        count += 1
        if points >= 2:
            converted += 1

    print(f"files_seen={count}")
    print(f"files_with_at_least_two_points={converted}")
    print(f"output_dir={output_dir}")


if __name__ == "__main__":
    main()
