from __future__ import annotations

import argparse
import csv
import os
import sys
import time
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Evaluate MARL4TS with configurable parameters.")
    parser.add_argument("--workspace-root", default=r"D:\all_traj_method")
    parser.add_argument("--traj-dir", default="MARL4TS/TrajData/Geolife_out")
    parser.add_argument("--env-amount", type=int, default=140)
    parser.add_argument("--test-start", type=int, default=120)
    parser.add_argument("--test-amount", type=int, default=20)
    parser.add_argument("--err-bounded", type=float, default=1e-5)
    parser.add_argument("--J", type=int, default=2)
    parser.add_argument("--k", type=int, default=5)
    parser.add_argument("--D", type=int, default=6)
    parser.add_argument("--constraint", type=int, default=50)
    parser.add_argument("--len_", type=int, default=2)
    parser.add_argument("--weights-ow", required=True)
    parser.add_argument("--weights-cw", required=True)
    parser.add_argument("--tag", default="run1")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    workspace_root = Path(args.workspace_root).resolve()
    marl_root = workspace_root / "MARL4TS"
    traj_dir = (workspace_root / args.traj_dir).resolve()
    output_dir = workspace_root / "test" / f"MARL4TS_eval_Geolife_{args.err_bounded}_{args.tag}"
    output_dir.mkdir(parents=True, exist_ok=True)
    os.environ["MPLCONFIGDIR"] = str((workspace_root / "tmp" / "mplconfig_marl4ts_eval").resolve())
    Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)

    sys.path.insert(0, str(marl_root))
    os.chdir(marl_root)

    from rl_states_drop_points_multi_agent_with_constraint import TrajComp_rl  # type: ignore
    from rl_brain_multi_agent_with_constraint import DeepQNetwork_OW, DeepQNetwork_CW  # type: ignore
    import data_utils as F  # type: ignore

    a_size_ow, s_size_ow, a_size_cw, s_size_cw = args.J, 2, args.k, args.k * args.len_
    env = TrajComp_rl(str(traj_dir) + os.sep, args.env_amount, a_size_ow, s_size_ow, a_size_cw, s_size_cw, args.len_, args.D, args.constraint)
    rl_ow = DeepQNetwork_OW(env.n_features_ow, env.n_actions_ow)
    rl_cw = DeepQNetwork_CW(env.n_features_cw, env.n_actions_cw)
    rl_ow.load(str((workspace_root / args.weights_ow).resolve()))
    rl_cw.load(str((workspace_root / args.weights_cw).resolve()))

    ratios = []
    errors = []
    input_points = []
    output_points = []
    wall_start = time.time()

    for episode in range(args.test_start, args.test_start + args.test_amount):
        env.reset(episode)
        while env.e < env.N:
            observation_ow = env.run_by_skip_value_6(episode)
            action_ow = rl_ow.online_act(observation_ow)
            observation_cw = env.step_choose_safe_3(episode, action_ow, args.err_bounded, args.k, "V")
            if bool(1 - env.conOpw):
                action_cw = rl_cw.online_act(observation_cw)
                env.step(episode, action_cw, "V")
        if env.simplified_index[-1] != env.N - 1:
            env.boundary(episode, "V")

        ratio = env.output(episode, "V")
        _, err = F.sed_error(env.ori_traj_set[episode], env.simplified_tra)
        ratios.append(ratio)
        errors.append(err)
        input_points.append(len(env.ori_traj_set[episode]))
        output_points.append(len(env.simplified_tra))

    wall_clock = time.time() - wall_start
    total_input = sum(input_points)
    total_output = sum(output_points)
    keep_ratio = (total_output / total_input) if total_input else 0.0
    saved_ratio = 1.0 - keep_ratio
    avg_error = (sum(errors) / len(errors)) if errors else float("inf")
    max_error = max(errors) if errors else float("inf")
    avg_ratio = (sum(ratios) / len(ratios)) if ratios else float("inf")

    summary_csv = output_dir / "marl4ts_metrics_summary.csv"
    run_info = output_dir / "run_info.txt"
    with summary_csv.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "algorithm",
                "dataset",
                "err_bounded",
                "processed_trajs",
                "avg_keep_ratio",
                "avg_error",
                "max_error",
                "total_input_points",
                "total_output_points",
                "keep_ratio",
                "saved_ratio",
                "overall_compression_ratio",
                "wall_clock_s",
            ]
        )
        writer.writerow(
            [
                "MARL4TS",
                "Geolife",
                args.err_bounded,
                len(ratios),
                f"{avg_ratio:.6f}",
                f"{avg_error:.6f}",
                f"{max_error:.6f}",
                total_input,
                total_output,
                f"{keep_ratio:.6f}",
                f"{saved_ratio:.6f}",
                f"{(1.0 / keep_ratio) if keep_ratio else 0.0:.6f}",
                f"{wall_clock:.6f}",
            ]
        )

    run_info.write_text(
        "\n".join(
            [
                "algorithm=MARL4TS",
                "dataset=Geolife",
                f"traj_dir={traj_dir}",
                f"env_amount={args.env_amount}",
                f"test_start={args.test_start}",
                f"test_amount={args.test_amount}",
                f"err_bounded={args.err_bounded}",
                f"J={args.J}",
                f"k={args.k}",
                f"D={args.D}",
                f"constraint={args.constraint}",
                f"len_={args.len_}",
                f"weights_ow={(workspace_root / args.weights_ow).resolve()}",
                f"weights_cw={(workspace_root / args.weights_cw).resolve()}",
                f"avg_keep_ratio={avg_ratio:.6f}",
                f"avg_error={avg_error:.6f}",
                f"max_error={max_error:.6f}",
                f"total_input_points={total_input}",
                f"total_output_points={total_output}",
                f"keep_ratio={keep_ratio:.6f}",
                f"saved_ratio={saved_ratio:.6f}",
                f"overall_compression_ratio={(1.0 / keep_ratio) if keep_ratio else 0.0:.6f}",
                f"wall_clock_s={wall_clock:.6f}",
                f"summary_csv={summary_csv}",
            ]
        ),
        encoding="utf-8",
    )

    print(f"processed_trajs={len(ratios)}")
    print(f"avg_error={avg_error}")
    print(f"summary_csv={summary_csv}")


if __name__ == "__main__":
    main()
