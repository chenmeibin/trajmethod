from __future__ import annotations

import argparse
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert simple whitespace-separated lat lon time trajectories into DimWeaver CSV files."
    )
    parser.add_argument("--input-dir", required=True, help="Directory containing input .plt files.")
    parser.add_argument("--output-dir", required=True, help="Directory for converted CSV files.")
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
                ts = int(float(parts[2]))
            except ValueError:
                continue
            yield ts, lon, lat


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
        dst = output_dir / rel.with_suffix(".csv")
        points = list(iter_points(src))
        if not points:
            continue

        dst.parent.mkdir(parents=True, exist_ok=True)
        with dst.open("w", encoding="utf-8", newline="\n") as out:
            out.write("t,x,y\n")
            prev_t = None
            for ts, lon, lat in points:
                if prev_t is not None and ts <= prev_t:
                    ts = prev_t + 1
                out.write(f"{ts},{lon:.15f},{lat:.15f}\n")
                prev_t = ts
                total_points += 1

        converted += 1

    print(f"files={converted}")
    print(f"points={total_points}")
    print(f"output_dir={output_dir}")


if __name__ == "__main__":
    main()
