from __future__ import annotations

import argparse
import csv
import os
import sys
import time
from pathlib import Path
import numpy as np


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Evaluate RLTS batch mode with configurable parameters.")
    parser.add_argument("--workspace-root", default=r"D:\all_traj_method")
    parser.add_argument("--traj-dir", default="RLTS/TrajData/Geolife_out")
    parser.add_argument("--env-amount", type=int, default=120)
    parser.add_argument("--test-start", type=int, default=100)
    parser.add_argument("--test-amount", type=int, default=20)
    parser.add_argument("--ratio", type=float, default=0.1)
    parser.add_argument("--a-size", type=int, default=3)
    parser.add_argument("--s-size", type=int, default=3)
    parser.add_argument("--checkpoint-dir", default="")
    parser.add_argument("--weights-npz", default="")
    parser.add_argument("--tag", default="smoke")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    workspace_root = Path(args.workspace_root).resolve()
    rlts_root = workspace_root / "RLTS"
    batch_root = rlts_root / "batch-rlts"
    traj_dir = (workspace_root / args.traj_dir).resolve()
    dataset_name = traj_dir.name
    checkpoint_dir = Path(args.checkpoint_dir).resolve()
    weights_npz = Path(args.weights_npz).resolve() if args.weights_npz else None
    output_dir = workspace_root / "test" / f"RLTS_batch_eval_{dataset_name}_ratio_{args.ratio}_{args.tag}"
    output_dir.mkdir(parents=True, exist_ok=True)
    os.environ["MPLCONFIGDIR"] = str((workspace_root / "tmp" / "mplconfig_rlts_eval").resolve())
    Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)

    sys.path.insert(0, str(batch_root))
    os.chdir(batch_root)

    from rl_env_inc import TrajComp  # type: ignore
    from rl_brain import PolicyGradient  # type: ignore

    env = TrajComp(str(traj_dir) + os.sep, args.env_amount, args.a_size, args.s_size)
    rl = PolicyGradient(env.n_features, env.n_actions)
    if weights_npz and weights_npz.exists():
        weights = np.load(weights_npz)
        rl.kernel_1 = weights["kernel_1"]
        rl.bias_1 = weights["bias_1"]
        rl.kernel_2 = weights["kernel_2"]
        rl.bias_2 = weights["bias_2"]
    else:
        rl.load(str(checkpoint_dir) + os.sep)

    errors = []
    input_points = []
    output_points = []
    wall_start = time.time()

    for episode in range(args.test_start, args.test_start + args.test_amount):
        buffer_size = int(args.ratio * len(env.ori_traj_set[episode]))
        if buffer_size < 3:
            continue
        steps, observation = env.reset(episode, buffer_size)
        for index in range(buffer_size, steps):
            done = index == steps - 1
            action = rl.quick_time_action(observation)
            observation, _ = env.step(episode, action, index, done, "V")
        errors.append(env.output(episode, "V"))

        kept = 1
        start = 0
        while start in env.F_ward:
            kept += 1
            start = env.F_ward[start][1]
        input_points.append(len(env.ori_traj_set[episode]))
        output_points.append(kept)

    wall_clock = time.time() - wall_start
    total_input = sum(input_points)
    total_output = sum(output_points)
    keep_ratio = (total_output / total_input) if total_input else 0.0
    saved_ratio = 1.0 - keep_ratio
    avg_error = (sum(errors) / len(errors)) if errors else float("inf")
    avg_time_per_traj = (wall_clock / len(errors)) if errors else 0.0

    summary_csv = output_dir / "rlts_batch_metrics_summary.csv"
    run_info = output_dir / "run_info.txt"

    with summary_csv.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "algorithm",
                "dataset",
                "ratio",
                "processed_trajs",
                "avg_error",
                "total_input_points",
                "total_output_points",
                "keep_ratio",
                "saved_ratio",
                "overall_compression_ratio",
                "wall_clock_s",
                "avg_time_per_traj_s",
            ]
        )
        writer.writerow(
            [
                "RLTS",
                dataset_name,
                args.ratio,
                len(errors),
                f"{avg_error:.6f}",
                total_input,
                total_output,
                f"{keep_ratio:.6f}",
                f"{saved_ratio:.6f}",
                f"{(1.0 / keep_ratio) if keep_ratio else 0.0:.6f}",
                f"{wall_clock:.6f}",
                f"{avg_time_per_traj:.6f}",
            ]
        )

    run_info.write_text(
        "\n".join(
            [
                "algorithm=RLTS",
                "mode=batch_eval",
                f"dataset={dataset_name}",
                f"traj_dir={traj_dir}",
                f"env_amount={args.env_amount}",
                f"test_start={args.test_start}",
                f"test_amount={args.test_amount}",
                f"ratio={args.ratio}",
                f"a_size={args.a_size}",
                f"s_size={args.s_size}",
                f"checkpoint_dir={checkpoint_dir}",
                f"weights_npz={weights_npz}",
                f"avg_error={avg_error:.6f}",
                f"total_input_points={total_input}",
                f"total_output_points={total_output}",
                f"keep_ratio={keep_ratio:.6f}",
                f"saved_ratio={saved_ratio:.6f}",
                f"overall_compression_ratio={(1.0 / keep_ratio) if keep_ratio else 0.0:.6f}",
                f"wall_clock_s={wall_clock:.6f}",
                f"avg_time_per_traj_s={avg_time_per_traj:.6f}",
                f"summary_csv={summary_csv}",
            ]
        ),
        encoding="utf-8",
    )

    print(f"processed_trajs={len(errors)}")
    print(f"avg_error={avg_error}")
    print(f"summary_csv={summary_csv}")


if __name__ == "__main__":
    main()
