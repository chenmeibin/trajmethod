from __future__ import annotations

import argparse
import datetime as dt
from pathlib import Path


HEADER_LINES = [
    "Geolife trajectory",
    "WGS 84",
    "Altitude is in Feet",
    "Reserved 3",
    "0,2,255,My Track,0,0,2,8421376",
    "0",
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert simple whitespace-separated lat lon epoch trajectories into Geolife-style .plt files."
    )
    parser.add_argument("--input-dir", required=True, help="Directory containing input .plt files.")
    parser.add_argument("--output-dir", required=True, help="Directory for converted Geolife-style .plt files.")
    return parser.parse_args()


def iter_points(path: Path):
    with path.open("r", encoding="utf-8", errors="ignore") as fh:
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
                epoch = int(float(parts[2]))
            except ValueError:
                continue
            yield lat, lon, epoch


def format_geolife_day(epoch: int) -> str:
    return f"{epoch / 86400.0:.12f}"


def format_datetime(epoch: int) -> tuple[str, str]:
    dt_value = dt.datetime.fromtimestamp(epoch, dt.UTC)
    return dt_value.strftime("%Y-%m-%d"), dt_value.strftime("%H:%M:%S")


def main() -> None:
    args = parse_args()
    input_dir = Path(args.input_dir).resolve()
    output_dir = Path(args.output_dir).resolve()
    output_dir.mkdir(parents=True, exist_ok=True)

    files = sorted(input_dir.rglob("*.plt"))
    converted = 0
    total_points = 0

    for src in files:
        rel = src.relative_to(input_dir)
        dst = output_dir / rel
        points = list(iter_points(src))
        if not points:
            continue

        dst.parent.mkdir(parents=True, exist_ok=True)
        with dst.open("w", encoding="utf-8", newline="\n") as out:
            for header in HEADER_LINES:
                out.write(header + "\n")

            prev_epoch = None
            for lat, lon, epoch in points:
                if prev_epoch is not None and epoch <= prev_epoch:
                    epoch = prev_epoch + 1
                date_text, time_text = format_datetime(epoch)
                day_text = format_geolife_day(epoch)
                out.write(f"{lat:.15f},{lon:.15f},0,0,{day_text},{date_text},{time_text}\n")
                prev_epoch = epoch
                total_points += 1

        converted += 1

    print(f"files={converted}")
    print(f"points={total_points}")
    print(f"output_dir={output_dir}")


if __name__ == "__main__":
    main()
