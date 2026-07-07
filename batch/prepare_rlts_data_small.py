from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path


@dataclass(frozen=True)
class DatasetConfig:
    source_name: str
    shared_name: str
    rlts_name: str
    kind: str
    pattern: str


DATASETS = [
    DatasetConfig("Geolife", "Geolife", "data_small_geolife_out", "geolife", "*.plt"),
    DatasetConfig("T-drive", "T-drive", "data_small_t-drive_out", "tdrive", "*.txt"),
    DatasetConfig("chengdu", "chengdu", "data_small_chengdu_out", "city", "*.txt"),
    DatasetConfig("wuxi", "wuxi", "data_small_wuxi_out", "city", "*.txt"),
]


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Convert data_small datasets into skwcl shared-converted format and RLTS numbered trajectory format."
    )
    parser.add_argument("--workspace-root", default=r"D:\all_traj_method")
    parser.add_argument("--min-points", type=int, default=2, help="Minimum points required to keep a trajectory.")
    parser.add_argument(
        "--datasets",
        nargs="+",
        choices=[cfg.source_name for cfg in DATASETS],
        help="Optional subset of datasets to convert.",
    )
    return parser.parse_args()


def parse_timestamp(text: str) -> int:
    text = text.strip()
    fmts = ("%Y-%m-%d %H:%M:%S.%f", "%Y-%m-%d %H:%M:%S")
    for fmt in fmts:
        try:
            return int(datetime.strptime(text, fmt).timestamp())
        except ValueError:
            continue
    raise ValueError(f"Unsupported timestamp: {text}")


def iter_geolife_points(path: Path):
    with path.open("r", encoding="utf-8", errors="ignore") as fh:
        for _ in range(6):
            next(fh, None)
        for raw_line in fh:
            line = raw_line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) < 7:
                continue
            lat = float(parts[0])
            lon = float(parts[1])
            epoch = parse_timestamp(parts[5].strip() + " " + parts[6].strip())
            yield lat, lon, epoch


def iter_tdrive_points(path: Path):
    with path.open("r", encoding="utf-8", errors="ignore") as fh:
        for raw_line in fh:
            line = raw_line.strip()
            if not line:
                continue
            parts = line.split(",")
            if len(parts) < 4:
                continue
            epoch = parse_timestamp(parts[1])
            lon = float(parts[2])
            lat = float(parts[3])
            yield lat, lon, epoch


def iter_city_points(path: Path):
    with path.open("r", encoding="utf-8", errors="ignore") as fh:
        first = True
        for raw_line in fh:
            line = raw_line.strip()
            if not line:
                continue
            if first:
                first = False
                continue
            parts = line.split(",")
            if len(parts) < 3:
                continue
            epoch = parse_timestamp(parts[0])
            lon = float(parts[1])
            lat = float(parts[2])
            yield lat, lon, epoch


def iter_points(path: Path, kind: str):
    if kind == "geolife":
        yield from iter_geolife_points(path)
    elif kind == "tdrive":
        yield from iter_tdrive_points(path)
    elif kind == "city":
        yield from iter_city_points(path)
    else:
        raise ValueError(f"Unsupported dataset kind: {kind}")


def ensure_strictly_increasing(points: list[tuple[float, float, int]]) -> list[tuple[float, float, int]]:
    fixed: list[tuple[float, float, int]] = []
    prev_epoch: int | None = None
    for lat, lon, epoch in points:
        if prev_epoch is not None and epoch <= prev_epoch:
            epoch = prev_epoch + 1
        fixed.append((lat, lon, epoch))
        prev_epoch = epoch
    return fixed


def write_shared_converted(out_path: Path, points: list[tuple[float, float, int]]) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8", newline="\n") as fh:
        for lat, lon, epoch in points:
            fh.write(f"{lat} {lon} {epoch}\n")


def write_rlts_numbered(out_path: Path, points: list[tuple[float, float, int]]) -> None:
    out_path.parent.mkdir(parents=True, exist_ok=True)
    with out_path.open("w", encoding="utf-8", newline="\n") as fh:
        for lat, lon, epoch in points:
            fh.write(f"{lat} {lon} {epoch}\n")


def clean_directory(directory: Path, pattern: str) -> None:
    if not directory.exists():
        return
    for path in directory.glob(pattern):
        if path.is_file():
            path.unlink()


def main() -> None:
    args = parse_args()
    workspace_root = Path(args.workspace_root).resolve()
    source_root = workspace_root / "data_small"
    shared_root = workspace_root / "skwcl" / "_shared_converted"
    rlts_root = workspace_root / "RLTS" / "TrajData"

    selected = {cfg.source_name for cfg in DATASETS} if not args.datasets else set(args.datasets)
    manifest_rows: list[dict[str, str | int]] = []

    for dataset in DATASETS:
        if dataset.source_name not in selected:
            continue
        src_dir = source_root / dataset.source_name
        shared_dir = shared_root / dataset.shared_name
        rlts_dir = rlts_root / dataset.rlts_name

        shared_dir.mkdir(parents=True, exist_ok=True)
        rlts_dir.mkdir(parents=True, exist_ok=True)

        clean_directory(shared_dir, "*.normalized.plt")
        clean_directory(rlts_dir, "*")

        source_files = sorted(
            p for p in src_dir.glob(dataset.pattern) if p.is_file() and p.name.lower() != "road_network.txt"
        )
        kept = 0
        skipped = 0
        total_points = 0

        for src_path in source_files:
            raw_points = list(iter_points(src_path, dataset.kind))
            points = ensure_strictly_increasing(raw_points)
            if len(points) < args.min_points:
                skipped += 1
                continue

            shared_name = src_path.stem + ".normalized.plt"
            shared_path = shared_dir / shared_name
            rlts_path = rlts_dir / str(kept)

            write_shared_converted(shared_path, points)
            write_rlts_numbered(rlts_path, points)

            manifest_rows.append(
                {
                    "dataset": dataset.source_name,
                    "source_file": str(src_path),
                    "shared_converted": str(shared_path),
                    "rlts_numbered": str(rlts_path),
                    "points": len(points),
                }
            )

            kept += 1
            total_points += len(points)

        summary_lines = [
            f"dataset={dataset.source_name}",
            f"source_files={len(source_files)}",
            f"kept_trajectories={kept}",
            f"skipped_trajectories={skipped}",
            f"total_points={total_points}",
            f"shared_dir={shared_dir}",
            f"rlts_dir={rlts_dir}",
            f"min_points={args.min_points}",
        ]
        (shared_dir / "prepare_summary.txt").write_text("\n".join(summary_lines), encoding="utf-8")
        (rlts_dir / "prepare_summary.txt").write_text("\n".join(summary_lines), encoding="utf-8")

        dataset_manifest_path = shared_dir / "prepare_manifest.csv"
        dataset_rows = [row for row in manifest_rows if row["dataset"] == dataset.source_name]
        with dataset_manifest_path.open("w", encoding="utf-8", newline="") as fh:
            writer = csv.DictWriter(
                fh,
                fieldnames=["dataset", "source_file", "shared_converted", "rlts_numbered", "points"],
            )
            writer.writeheader()
            writer.writerows(dataset_rows)

    manifest_tag = "_".join(sorted(name.lower().replace("-", "_") for name in selected))
    manifest_path = shared_root / f"data_small_rlts_manifest_{manifest_tag}.csv"
    with manifest_path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(
            fh,
            fieldnames=["dataset", "source_file", "shared_converted", "rlts_numbered", "points"],
        )
        writer.writeheader()
        writer.writerows(manifest_rows)

    print(f"manifest={manifest_path}")
    print(f"rows={len(manifest_rows)}")


if __name__ == "__main__":
    main()
