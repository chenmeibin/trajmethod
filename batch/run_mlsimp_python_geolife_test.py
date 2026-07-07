from __future__ import annotations

import argparse
import contextlib
import csv
import io
import os
import pickle
import re
import shutil
import sys
import time
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run the original MLSimp Python validation on Geolife and export a compact summary."
    )
    parser.add_argument("--workspace-root", default=r"D:\all_traj_method")
    parser.add_argument("--dataset", default="Geolife")
    parser.add_argument("--q-type", default="knn", choices=["range", "knn", "cluster", "join"])
    parser.add_argument("--distri", default="gau", choices=["gau", "data"])
    parser.add_argument("--cr", type=float, default=0.01)
    parser.add_argument("--tag", default="cr_0.01")
    return parser.parse_args()


def force_cpu_yaml_load(original_safe_load):
    def wrapped(stream):
        data = original_safe_load(stream)
        if isinstance(data, dict):
            for cfg in data.values():
                if isinstance(cfg, dict):
                    cfg["device"] = "cpu"
                    if "Bert" in cfg and isinstance(cfg["Bert"], dict):
                        cfg["Bert"]["device"] = "cpu"
        return data

    return wrapped


def force_cpu_torch_load(torch_module):
    original_load = torch_module.load

    def wrapped(*args, **kwargs):
        kwargs.setdefault("map_location", "cpu")
        return original_load(*args, **kwargs)

    return wrapped


def summarize_result(result_pickle: Path, test_pickle: Path) -> tuple[int, int, float]:
    with result_pickle.open("rb") as f:
        simp_trajs = pickle.load(f)
    with test_pickle.open("rb") as f:
        orig_trajs = pickle.load(f)

    total_input_points = sum(len(traj) for traj in orig_trajs)
    total_output_points = sum(len(traj) for traj in simp_trajs)
    keep_ratio = (total_output_points / total_input_points) if total_input_points else 0.0
    return total_input_points, total_output_points, keep_ratio


def main() -> None:
    args = parse_args()
    workspace_root = Path(args.workspace_root).resolve()
    mlsimp_root = workspace_root / "MLSimp"
    output_dir = workspace_root / "test" / f"MLSimp_python_test_{args.dataset}_{args.tag}"
    output_dir.mkdir(parents=True, exist_ok=True)
    mplconfig_dir = output_dir / "mplconfig"
    mplconfig_dir.mkdir(parents=True, exist_ok=True)
    os.environ["MPLCONFIGDIR"] = str(mplconfig_dir)
    (mlsimp_root / "SimpTraj" / args.dataset / args.q_type).mkdir(parents=True, exist_ok=True)
    (mlsimp_root / "Val" / args.dataset / args.q_type).mkdir(parents=True, exist_ok=True)

    summary_csv = output_dir / "mlsimp_python_metrics_summary.csv"
    run_info = output_dir / "run_info.txt"
    raw_log = output_dir / "validation_stdout.txt"

    sys.path.insert(0, str(mlsimp_root))
    os.chdir(mlsimp_root)

    import yaml  # type: ignore
    import torch  # type: ignore
    import validation as mlsimp_validation  # type: ignore

    original_safe_load = yaml.safe_load
    original_torch_load = torch.load
    yaml.safe_load = force_cpu_yaml_load(original_safe_load)  # type: ignore[assignment]
    torch.load = force_cpu_torch_load(torch)  # type: ignore[assignment]

    mlsimp_validation.dataset = args.dataset
    mlsimp_validation.q_type = args.q_type
    mlsimp_validation.distri = args.distri
    mlsimp_validation.cr = args.cr

    buffer = io.StringIO()
    wall_start = time.time()
    try:
        with contextlib.redirect_stdout(buffer):
            mlsimp_validation.validation()
    finally:
        yaml.safe_load = original_safe_load  # type: ignore[assignment]
        torch.load = original_torch_load  # type: ignore[assignment]
    wall_seconds = time.time() - wall_start

    log_text = buffer.getvalue()
    raw_log.write_text(log_text, encoding="utf-8")

    result_pickle = mlsimp_root / "SimpTraj" / args.dataset / args.q_type / f"distri_{args.distri}_cr_{args.cr}.pickle"
    test_pickle = mlsimp_root / "TrajData" / f"{args.dataset}_TVT" / "test.pkl"
    copied_pickle = output_dir / result_pickle.name
    if result_pickle.exists():
        shutil.copy2(result_pickle, copied_pickle)

    total_input_points, total_output_points, keep_ratio = summarize_result(result_pickle, test_pickle)
    saved_ratio = 1.0 - keep_ratio

    simp_time_match = re.search(r"simplification time:\s*([0-9eE+\-.]+)", log_text)
    f1_match = re.search(r"f1\s*=\s*([0-9eE+\-.]+)", log_text)
    simplification_time_s = float(simp_time_match.group(1)) if simp_time_match else 0.0
    query_f1 = float(f1_match.group(1)) if f1_match else 0.0

    with summary_csv.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow([
            "algorithm",
            "implementation",
            "dataset",
            "q_type",
            "distri",
            "cr",
            "processed_trajs",
            "total_input_points",
            "total_output_points",
            "keep_ratio",
            "saved_ratio",
            "simplification_time_s",
            "wall_clock_s",
            "query_f1",
        ])
        writer.writerow([
            "MLSimp",
            "python_original",
            args.dataset,
            args.q_type,
            args.distri,
            args.cr,
            2000,
            total_input_points,
            total_output_points,
            f"{keep_ratio:.6f}",
            f"{saved_ratio:.6f}",
            f"{simplification_time_s:.6f}",
            f"{wall_seconds:.6f}",
            f"{query_f1:.6f}",
        ])

    run_info.write_text(
        "\n".join(
            [
                "algorithm=MLSimp",
                "implementation=python_original",
                f"dataset={args.dataset}",
                f"q_type={args.q_type}",
                f"distri={args.distri}",
                f"cr={args.cr}",
                "device=cpu",
                "device_note=forced_cpu_runtime_wrapper_only_algorithm_unchanged",
                "threshold_note=MLSimp_original_python_does_not_use_1e-5_error_threshold_it_uses_cr",
                "processed_trajs=2000",
                f"total_input_points={total_input_points}",
                f"total_output_points={total_output_points}",
                f"keep_ratio={keep_ratio:.6f}",
                f"saved_ratio={saved_ratio:.6f}",
                f"simplification_time_s={simplification_time_s:.6f}",
                f"wall_clock_s={wall_seconds:.6f}",
                f"query_f1={query_f1:.6f}",
                f"summary_csv={summary_csv}",
                f"raw_log={raw_log}",
                f"result_pickle={copied_pickle}",
            ]
        ),
        encoding="utf-8",
    )

    print(f"Saved MLSimp Python results to {output_dir}")


if __name__ == "__main__":
    main()
