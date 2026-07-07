from __future__ import annotations

import argparse
import csv
import datetime as dt
from pathlib import Path


DATASETS = {
    "ais": Path(r"D:\all_traj_method\Data\ais"),
    "trajair": Path(r"D:\all_traj_method\Data\trajair"),
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert simple txt trajectories into whitespace-separated lat lon epoch .plt files."
    )
    parser.add_argument(
        "--output-root",
        default=r"D:\all_traj_method\skwcl\_shared_converted",
        help="Root directory that will contain one converted subdirectory per dataset.",
    )
    parser.add_argument(
        "--datasets",
        nargs="*",
        default=sorted(DATASETS),
        help="Dataset keys to prepare. Defaults to all supported datasets.",
    )
    return parser.parse_args()


def parse_timestamp(text: str) -> int:
    normalized = text.strip()
    for fmt in ("%Y-%m-%d %H:%M:%S.%f", "%Y-%m-%d %H:%M:%S"):
        try:
            value = dt.datetime.strptime(normalized, fmt)
            return int(value.timestamp())
        except ValueError:
            continue
    raise ValueError(f"Unsupported timestamp: {text!r}")


def convert_file(src: Path, dst: Path) -> int:
    rows: list[str] = []
    with src.open("r", encoding="utf-8", errors="ignore") as fh:
        for line_index, raw_line in enumerate(fh):
            line = raw_line.strip()
            if not line:
                continue
            if line_index == 0:
                continue
            parts = next(csv.reader([line]))
            if len(parts) < 3:
                continue
            try:
                epoch = parse_timestamp(parts[0])
                lon = float(parts[1])
                lat = float(parts[2])
            except ValueError:
                continue
            rows.append(f"{lat} {lon} {epoch}")

    if len(rows) < 2:
        return 0

    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_text("\n".join(rows) + "\n", encoding="utf-8")
    return len(rows)


def main() -> int:
    args = parse_args()
    output_root = Path(args.output_root).resolve()
    output_root.mkdir(parents=True, exist_ok=True)

    manifest_rows: list[dict[str, object]] = []
    selected = [name for name in args.datasets if name in DATASETS]
    unknown = sorted(set(args.datasets) - set(selected))
    if unknown:
        raise ValueError(f"Unsupported datasets: {', '.join(unknown)}")

    for dataset_name in selected:
        source_root = DATASETS[dataset_name]
        if not source_root.exists():
            raise FileNotFoundError(f"dataset source not found: {source_root}")
        target_root = output_root / dataset_name
        files = sorted(source_root.rglob("*.txt"))
        if not files:
            raise FileNotFoundError(f"no .txt files found under {source_root}")

        for src in files:
            relative = src.relative_to(source_root).with_suffix(".normalized.plt")
            dst = target_root / relative
            points = convert_file(src, dst)
            manifest_rows.append(
                {
                    "dataset": dataset_name,
                    "source_file": str(src),
                    "converted_file": str(dst),
                    "point_count": points,
                }
            )

    manifest_path = output_root / "simple_converted_manifest.csv"
    with manifest_path.open("w", encoding="utf-8", newline="") as fout:
        writer = csv.DictWriter(
            fout,
            fieldnames=["dataset", "source_file", "converted_file", "point_count"],
            lineterminator="\n",
        )
        writer.writeheader()
        writer.writerows(manifest_rows)

    print(f"prepared_files={len(manifest_rows)}")
    print(f"manifest={manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
