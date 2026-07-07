from __future__ import annotations

import argparse
import json
import math
import os
import pickle
import sys
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Prepare S3 Geolife dataset files with Geolife-sized split defaults, without editing S3 source."
    )
    parser.add_argument("--workspace-root", default=r"D:\all_traj_method")
    parser.add_argument("--pos-file", default="S3/datasets/geolife/geolife.pos")
    parser.add_argument("--min-length", type=int, default=30)
    parser.add_argument("--max-length", type=int, default=400)
    parser.add_argument("--train-ratio", type=float, default=0.60)
    parser.add_argument("--val-ratio", type=float, default=0.20)
    parser.add_argument("--eval-ratio", type=float, default=0.20)
    parser.add_argument("--ntrain", type=int)
    parser.add_argument("--nval", type=int)
    parser.add_argument("--neval", type=int)
    return parser.parse_args()


def count_candidate_segments(pos_file: Path, min_length: int, max_length: int) -> int:
    total = 0
    with pos_file.open("r", encoding="utf-8") as f:
        for line in f:
            points = json.loads(line)
            if len(points) > max_length:
                st = 0
                while st + max_length < len(points):
                    seg_len = len(points[st:st + max_length])
                    if min_length <= seg_len <= max_length:
                        total += 1
                    st += max_length
            else:
                if min_length <= len(points) <= max_length:
                    total += 1
    return total


def derive_split_counts(total: int, train_ratio: float, val_ratio: float, eval_ratio: float) -> tuple[int, int, int]:
    ratio_sum = train_ratio + val_ratio + eval_ratio
    if ratio_sum <= 0:
        raise ValueError("Split ratios must sum to a positive value.")

    train_ratio /= ratio_sum
    val_ratio /= ratio_sum
    eval_ratio /= ratio_sum

    ntrain = int(math.floor(total * train_ratio))
    nval = int(math.floor(total * val_ratio))
    neval = total - ntrain - nval
    return ntrain, nval, neval


def main() -> None:
    args = parse_args()
    workspace_root = Path(args.workspace_root).resolve()
    s3_root = workspace_root / "S3"
    dataset_dir = s3_root / "datasets" / "geolife"
    pos_file = workspace_root / args.pos_file

    sys.path.insert(0, str(s3_root))
    sys.path.insert(0, str(s3_root / "preprocess"))

    from preprocess.SpatialRegionTools import SpacialRegion, makeVocab  # type: ignore
    import preprocess.createDataset as create_dataset  # type: ignore

    total_candidates = count_candidate_segments(pos_file, args.min_length, args.max_length)

    if args.ntrain is not None and args.nval is not None and args.neval is not None:
        ntrain, nval, neval = args.ntrain, args.nval, args.neval
    else:
        ntrain, nval, neval = derive_split_counts(
            total_candidates,
            args.train_ratio,
            args.val_ratio,
            args.eval_ratio,
        )

    dataset_dir.mkdir(parents=True, exist_ok=True)

    # Use the Geolife-specific region settings preserved in S3 comments,
    # while keeping the original preprocessing logic untouched.
    region = SpacialRegion(
        minlon=115.7001,
        minlat=39.4,
        maxlon=117.39994,
        maxlat=41.59471,
        xstep=350.0,
        ystep=350.0,
        minfreq=100,
        maxvocab_size=8000,
        k=10,
        vocab_start=4,
    )
    num_out_region = makeVocab(region, str(pos_file))

    with (dataset_dir / "pickle.txt").open("wb") as f:
        pickle.dump(pickle.dumps(region), f)

    old_cwd = Path.cwd()
    try:
        os.chdir(s3_root / "preprocess")
        create_dataset.createTrainVal(
            region,
            str(pos_file),
            ntrain=ntrain,
            nval=nval,
            neval=neval,
            min_length=args.min_length,
            max_length=args.max_length,
        )
    finally:
        os.chdir(old_cwd)

    summary = {
        "algorithm": "S3",
        "dataset": "Geolife",
        "pos_file": str(pos_file),
        "dataset_dir": str(dataset_dir),
        "min_length": args.min_length,
        "max_length": args.max_length,
        "total_candidate_segments": total_candidates,
        "ntrain": ntrain,
        "nval": nval,
        "neval": neval,
        "split_ratios": {
            "train_ratio": args.train_ratio,
            "val_ratio": args.val_ratio,
            "eval_ratio": args.eval_ratio,
        },
        "region": {
            "minlon": region.minlon,
            "minlat": region.minlat,
            "maxlon": region.maxlon,
            "maxlat": region.maxlat,
            "xstep": region.xstep,
            "ystep": region.ystep,
            "minfreq": region.minfreq,
            "maxvocab_size": region.maxvocab_size,
            "k": region.k,
            "vocab_start": region.vocab_start,
        },
        "num_out_region_points": num_out_region,
        "notes": [
            "Prepared without modifying S3 source files.",
            "Used Geolife region settings from comments in SpatialRegionTools.py.",
        ],
    }
    (dataset_dir / "prepare_dataset_summary.json").write_text(
        json.dumps(summary, indent=2),
        encoding="utf-8",
    )

    print(f"total_candidate_segments={total_candidates}")
    print(f"ntrain={ntrain}")
    print(f"nval={nval}")
    print(f"neval={neval}")
    print(f"pickle={dataset_dir / 'pickle.txt'}")
    print(f"train={dataset_dir / 'train.src'}")
    print(f"val={dataset_dir / 'val.src'}")
    print(f"eval={dataset_dir / 'eval.src'}")


if __name__ == "__main__":
    main()
