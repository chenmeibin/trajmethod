from __future__ import annotations

import argparse
import csv
import os
import sys
import time
from pathlib import Path
import shutil
import numpy as np


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Train RLTS batch mode with configurable parameters.")
    parser.add_argument("--workspace-root", default=r"D:\all_traj_method")
    parser.add_argument("--traj-dir", default="RLTS/TrajData/Geolife_out")
    parser.add_argument("--train-amount", type=int, default=100)
    parser.add_argument("--valid-amount", type=int, default=20)
    parser.add_argument("--rounds", type=int, default=1)
    parser.add_argument("--ratio", type=float, default=0.1)
    parser.add_argument("--a-size", type=int, default=3)
    parser.add_argument("--s-size", type=int, default=3)
    parser.add_argument("--show-every", type=int, default=20)
    parser.add_argument("--tag", default="smoke")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    workspace_root = Path(args.workspace_root).resolve()
    rlts_root = workspace_root / "RLTS"
    batch_root = rlts_root / "batch-rlts"
    traj_dir = (workspace_root / args.traj_dir).resolve()
    output_dir = workspace_root / "test" / f"RLTS_batch_train_Geolife_ratio_{args.ratio}_{args.tag}"
    checkpoint_dir = rlts_root / "batch-rlts" / "save" / f"rlts_{args.tag}"
    output_dir.mkdir(parents=True, exist_ok=True)
    checkpoint_dir.mkdir(parents=True, exist_ok=True)
    for old in checkpoint_dir.glob("*"):
        try:
            if old.is_file():
                old.unlink()
            elif old.is_dir():
                shutil.rmtree(old)
        except PermissionError:
            pass
    os.environ["MPLCONFIGDIR"] = str((workspace_root / "tmp" / "mplconfig_rlts_train").resolve())
    Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)

    sys.path.insert(0, str(batch_root))
    os.chdir(batch_root)

    from rl_env_inc import TrajComp  # type: ignore
    from rl_brain import PolicyGradient  # type: ignore

    env = TrajComp(str(traj_dir) + os.sep, args.train_amount + args.valid_amount, args.a_size, args.s_size)
    rl = PolicyGradient(env.n_features, env.n_actions)

    training_csv = output_dir / "training_log.csv"
    summary_txt = output_dir / "run_info.txt"
    best_error = float("inf")
    save_warning = ""
    records: list[tuple[int, int, float, float]] = []
    weights_npz = output_dir / "best_policy_weights.npz"
    training_csv.write_text("round,episode,train_error,valid_error\n", encoding="utf-8")

    def export_policy_weights() -> None:
        graph = rl.sess.graph
        kernel_1 = rl.sess.run(graph.get_tensor_by_name("fc1/kernel:0"))
        bias_1 = rl.sess.run(graph.get_tensor_by_name("fc1/bias:0"))
        kernel_2 = rl.sess.run(graph.get_tensor_by_name("fc2/kernel:0"))
        bias_2 = rl.sess.run(graph.get_tensor_by_name("fc2/bias:0"))
        np.savez(
            weights_npz,
            kernel_1=kernel_1,
            bias_1=bias_1,
            kernel_2=kernel_2,
            bias_2=bias_2,
            n_features=env.n_features,
            n_actions=env.n_actions,
        )

    def run_validation() -> float:
        eva = []
        for episode in range(args.train_amount, args.train_amount + args.valid_amount):
            buffer_size = int(args.ratio * len(env.ori_traj_set[episode]))
            if buffer_size < 3:
                continue
            steps, observation = env.reset(episode, buffer_size)
            for index in range(buffer_size, steps):
                done = index == steps - 1
                action = rl.fix_choose_action(observation)
                observation, _ = env.step(episode, action, index, done, "V")
            eva.append(env.output(episode, "V"))
        return sum(eva) / len(eva) if eva else float("inf")

    wall_start = time.time()
    for round_idx in range(args.rounds):
        for episode in range(args.train_amount):
            buffer_size = int(args.ratio * len(env.ori_traj_set[episode]))
            if buffer_size < 3:
                continue
            steps, observation = env.reset(episode, buffer_size)
            for index in range(buffer_size, steps):
                done = index == steps - 1
                action = rl.pro_choose_action(observation)
                observation_, reward = env.step(episode, action, index, done, "T")
                rl.store_transition(observation, action, reward)
                if done:
                    rl.learn()
                    break
                observation = observation_

            train_error = env.output(episode, "T")
            if episode % args.show_every == 0:
                valid_error = run_validation()
                records.append((round_idx + 1, episode, train_error, valid_error))
                with training_csv.open("a", encoding="utf-8", newline="") as f:
                    f.write(f"{round_idx + 1},{episode},{train_error},{valid_error}\n")
                if valid_error < best_error:
                    best_error = valid_error
                    export_policy_weights()
                    try:
                        rl.save(str(checkpoint_dir / "trained_model.ckpt"))
                    except Exception as exc:
                        save_warning = f"{type(exc).__name__}: {exc}"
                        print(f"checkpoint_save_warning={save_warning}")
                print(f"round={round_idx + 1} episode={episode} train_error={train_error} valid_error={valid_error}")

    wall_clock = time.time() - wall_start

    with training_csv.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["round", "episode", "train_error", "valid_error"])
        writer.writerows(records)

    summary_txt.write_text(
        "\n".join(
            [
                "algorithm=RLTS",
                "mode=batch_train",
                f"traj_dir={traj_dir}",
                f"train_amount={args.train_amount}",
                f"valid_amount={args.valid_amount}",
                f"rounds={args.rounds}",
                f"ratio={args.ratio}",
                f"a_size={args.a_size}",
                f"s_size={args.s_size}",
                f"show_every={args.show_every}",
                f"best_valid_error={best_error}",
                f"checkpoint_save_warning={save_warning}",
                f"wall_clock_s={wall_clock:.6f}",
                f"checkpoint_dir={checkpoint_dir}",
                f"weights_npz={weights_npz}",
                f"training_csv={training_csv}",
            ]
        ),
        encoding="utf-8",
    )

    print(f"best_valid_error={best_error}")
    print(f"checkpoint_dir={checkpoint_dir}")
    print(f"weights_npz={weights_npz}")
    print(f"training_csv={training_csv}")


if __name__ == "__main__":
    main()
