from __future__ import annotations

import argparse
from pathlib import Path

from pyproj import Proj, Transformer


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert raw Geolife .plt files into PILOT-C-style t,x,y trajectories."
    )
    parser.add_argument("--input-dir", required=True, help="Root directory containing Geolife .plt files.")
    parser.add_argument("--output-dir", required=True, help="Directory to write converted CSV-like txt files.")
    parser.add_argument("--utm-zone", type=int, default=35, help="UTM zone used by original PILOT-C preprocessing.")
    return parser.parse_args()


def convert_file(src: Path, dst: Path, transformer: Transformer) -> int:
    lines: list[str] = []
    last_t: int | None = None
    with src.open("r", encoding="utf-8") as f:
        for idx, raw_line in enumerate(f):
            if idx < 6:
                continue
            parts = raw_line.strip().split(",")
            if len(parts) < 7:
                continue
            try:
                lat = float(parts[0])
                lon = float(parts[1])
                t = round(float(parts[4]) * 86400)
            except ValueError:
                continue

            if last_t is not None and t == last_t:
                continue

            x, y = transformer.transform(lon, lat)
            lines.append(f"{t},{x},{y}")
            last_t = t

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text("\n".join(lines), encoding="utf-8")
    return len(lines)


def main() -> None:
    args = parse_args()
    input_dir = Path(args.input_dir).resolve()
    output_dir = Path(args.output_dir).resolve()

    if not input_dir.exists():
        raise FileNotFoundError(f"Input dir not found: {input_dir}")

    wgs_proj = Proj(proj="latlong", datum="WGS84")
    utm_proj = Proj(proj="utm", zone=args.utm_zone, south=False)
    transformer = Transformer.from_proj(wgs_proj, utm_proj, always_xy=True)

    count = 0
    converted = 0
    for src in sorted(input_dir.rglob("*.plt")):
        rel = src.relative_to(input_dir)
        dst = output_dir / rel.with_suffix(".txt")
        points = convert_file(src, dst, transformer)
        count += 1
        if points >= 2:
            converted += 1

    print(f"files_seen={count}")
    print(f"files_with_at_least_two_points={converted}")
    print(f"output_dir={output_dir}")


if __name__ == "__main__":
    main()
