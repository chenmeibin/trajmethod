from __future__ import annotations

import argparse
import csv
import datetime as dt
from pathlib import Path


DATASETS = {
    "t-drive": Path(r"D:\all_traj_method\skwcl\TR-TR_result\converted"),
    "geolife": Path(r"D:\all_traj_method\skwcl\TR-TR_result\geolife\converted"),
    "chengdu": Path(r"D:\all_traj_method\skwcl\TR-TR_result\chengdu\converted"),
    "wuxi": Path(r"D:\all_traj_method\Data\TD-TR_wuxi_converted"),
    "ais": Path(r"D:\all_traj_method\skwcl\_shared_converted\ais"),
    "trajair": Path(r"D:\all_traj_method\skwcl\_shared_converted\trajair"),
}


def ensure_parent(path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)


def convert_epoch_to_text(epoch_text: str) -> str:
    ts = int(float(epoch_text))
    return dt.datetime.fromtimestamp(ts).strftime("%Y-%m-%d %H:%M:%S")


def convert_file(src: Path, dst: Path) -> int:
    ensure_parent(dst)
    point_count = 0
    with src.open("r", encoding="utf-8") as fin, dst.open("w", encoding="utf-8", newline="") as fout:
        writer = csv.writer(fout, lineterminator="\n")
        for raw_line in fin:
            line = raw_line.strip()
            if not line:
                continue
            parts = line.split()
            if len(parts) < 3:
                raise ValueError(f"unexpected input format in {src}: {raw_line!r}")
            lat_text, lon_text, epoch_text = parts[0], parts[1], parts[2]
            writer.writerow(["1", convert_epoch_to_text(epoch_text), lon_text, lat_text])
            point_count += 1
    return point_count


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output-root",
        default=r"D:\all_traj_method\batch\DPTS_prepared",
        help="Directory for prepared DPTS inputs.",
    )
    parser.add_argument(
        "--datasets",
        nargs="*",
        default=list(DATASETS),
        help="Dataset keys to prepare. Defaults to all known datasets.",
    )
    args = parser.parse_args()

    output_root = Path(args.output_root)
    output_root.mkdir(parents=True, exist_ok=True)

    manifest_rows: list[dict[str, object]] = []

    selected = [name for name in args.datasets if name in DATASETS]
    unknown = sorted(set(args.datasets) - set(selected))
    if unknown:
        raise ValueError(f"unsupported datasets: {', '.join(unknown)}")

    for dataset_name in selected:
        source_root = DATASETS[dataset_name]
        if not source_root.exists():
            raise FileNotFoundError(f"dataset source not found: {source_root}")

        target_root = output_root / dataset_name
        files = sorted(source_root.rglob("*.plt"))
        if not files:
            raise FileNotFoundError(f"no .plt files found under {source_root}")

        for src in files:
            relative = src.relative_to(source_root).with_suffix(".txt")
            dst = target_root / relative
            points = convert_file(src, dst)
            manifest_rows.append(
                {
                    "dataset": dataset_name,
                    "source_file": str(src),
                    "prepared_file": str(dst),
                    "point_count": points,
                }
            )

    manifest_path = output_root / "manifest.csv"
    with manifest_path.open("w", encoding="utf-8", newline="") as fout:
        writer = csv.DictWriter(
            fout,
            fieldnames=["dataset", "source_file", "prepared_file", "point_count"],
            lineterminator="\n",
        )
        writer.writeheader()
        writer.writerows(manifest_rows)

    print(f"prepared_files={len(manifest_rows)}")
    print(f"manifest={manifest_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
