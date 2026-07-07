import argparse
import pathlib
import time


def parse_args():
    parser = argparse.ArgumentParser(description="Prepare Geolife_out for RL4QDTS.")
    parser.add_argument("--workspace-root", required=True)
    return parser.parse_args()


def load_points(path: pathlib.Path):
    points = []
    with path.open("r", encoding="utf-8") as f:
        for idx, line in enumerate(f):
            if idx < 6:
                continue
            parts = line.strip().split(",")
            if len(parts) < 7:
                continue
            dt = f"{parts[5]} {parts[6]}"
            ts = int(time.mktime(time.strptime(dt, "%Y-%m-%d %H:%M:%S")))
            points.append((parts[0], parts[1], str(ts)))
    return points


def main():
    args = parse_args()
    root = pathlib.Path(args.workspace_root)
    src_dir = root / "data" / "Geolife"
    out_dir = root / "RL4QDTS" / "TrajData" / "Geolife_out"
    out_dir.mkdir(parents=True, exist_ok=True)

    for old in out_dir.iterdir():
        if old.is_file():
            old.unlink()

    kept = 0
    skipped = 0
    total_points = 0
    min_points = None
    max_points = 0

    for src in sorted(src_dir.glob("*.plt")):
        points = load_points(src)
        if len(points) <= 30:
            skipped += 1
            continue
        dst = out_dir / str(kept)
        with dst.open("w", encoding="utf-8") as f:
            for lat, lon, ts in points:
                f.write(f"{lat} {lon} {ts}\n")
        kept += 1
        total_points += len(points)
        min_points = len(points) if min_points is None else min(min_points, len(points))
        max_points = max(max_points, len(points))

    summary = out_dir / "prepare_summary.txt"
    with summary.open("w", encoding="utf-8") as f:
        f.write(f"source_dir={src_dir}\n")
        f.write(f"kept_trajectories={kept}\n")
        f.write(f"skipped_trajectories={skipped}\n")
        f.write(f"total_points={total_points}\n")
        f.write(f"min_points={min_points or 0}\n")
        f.write(f"max_points={max_points}\n")

    print(f"kept_trajectories={kept}")
    print(f"summary={summary}")


if __name__ == "__main__":
    main()
