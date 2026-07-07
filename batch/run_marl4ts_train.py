from __future__ import annotations

import argparse
import csv
import os
import sys
import time
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Train MARL4TS with configurable parameters.")
    parser.add_argument("--workspace-root", default=r"D:\all_traj_method")
    parser.add_argument("--traj-dir", default="MARL4TS/TrajData/Geolife_out")
    parser.add_argument("--train-amount", type=int, default=100)
    parser.add_argument("--valid-amount", type=int, default=20)
    parser.add_argument("--err-bounded", type=float, default=1e-5)
    parser.add_argument("--J", type=int, default=2)
    parser.add_argument("--k", type=int, default=5)
    parser.add_argument("--D", type=int, default=6)
    parser.add_argument("--constraint", type=int, default=50)
    parser.add_argument("--len_", type=int, default=2)
    parser.add_argument("--show-every", type=int, default=10)
    parser.add_argument("--batch-size", type=int, default=32)
    parser.add_argument("--train-op", type=int, default=10)
    parser.add_argument("--tag", default="run1")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    workspace_root = Path(args.workspace_root).resolve()
    marl_root = workspace_root / "MARL4TS"
    traj_dir = (workspace_root / args.traj_dir).resolve()
    output_dir = workspace_root / "test" / f"MARL4TS_train_Geolife_{args.err_bounded}_{args.tag}"
    output_dir.mkdir(parents=True, exist_ok=True)
    os.environ["MPLCONFIGDIR"] = str((workspace_root / "tmp" / "mplconfig_marl4ts_train").resolve())
    Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)

    sys.path.insert(0, str(marl_root))
    os.chdir(marl_root)

    from rl_states_drop_points_multi_agent_with_constraint import TrajComp_rl  # type: ignore
    from rl_brain_multi_agent_with_constraint import DeepQNetwork_OW, DeepQNetwork_CW  # type: ignore

    a_size_ow, s_size_ow, a_size_cw, s_size_cw = args.J, 2, args.k, args.k * args.len_
    env = TrajComp_rl(str(traj_dir) + os.sep, args.train_amount + args.valid_amount, a_size_ow, s_size_ow, a_size_cw, s_size_cw, args.len_, args.D, args.constraint)
    rl_ow = DeepQNetwork_OW(env.n_features_ow, env.n_actions_ow)
    rl_cw = DeepQNetwork_CW(env.n_features_cw, env.n_actions_cw)

    weights_ow = output_dir / "RL_OW_best.weights.h5"
    weights_cw = output_dir / "RL_CW_best.weights.h5"
    log_csv = output_dir / "training_log.csv"
    run_info = output_dir / "run_info.txt"
    progress_txt = output_dir / "progress.txt"
    best_valid_ratio = float("inf")
    records: list[tuple[int, float, float]] = []

    with log_csv.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(["episode", "train_ratio", "valid_ratio"])

    def run_validation() -> float:
        eva = []
        for episode in range(args.train_amount, args.train_amount + args.valid_amount):
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
            eva.append(env.output(episode, "V"))
        return sum(eva) / len(eva) if eva else float("inf")

    wall_start = time.time()
    for episode in range(args.train_amount):
        env.reset(episode)
        obs_ow, act_ow, transition_ow = [], [], []
        obs_cw, act_cw, transition_cw = [], [], []

        while env.e < env.N:
            observation_ow = env.run_by_skip_value_6(episode)
            obs_ow.append(observation_ow)
            action_ow = rl_ow.act(observation_ow)
            observation_cw = env.step_choose_safe_3(episode, action_ow, args.err_bounded, args.k, "T")

            if bool(1 - env.conOpw):
                obs_cw.append(observation_cw)
                action_cw = rl_cw.act(observation_cw)
                reward = env.step(episode, action_cw, "T")

                if len(obs_ow) >= 2:
                    transition_ow.append([obs_ow[-2], act_ow[-1], obs_ow[-1], False])
                    if reward is not None:
                        for tr in transition_ow:
                            rl_ow.remember(tr[0], tr[1], reward, tr[2], tr[3])
                        transition_ow.clear()

                if len(obs_cw) >= 2:
                    transition_cw.append([obs_cw[-2], act_cw[-1], obs_cw[-1], False])
                    if reward is not None:
                        for tr in transition_cw:
                            rl_cw.remember(tr[0], tr[1], reward, tr[2], tr[3])
                        transition_cw.clear()

                act_cw.append(action_cw)
                if len(rl_cw.memory) > args.batch_size:
                    rl_cw.replay(args.batch_size)

            act_ow.append(action_ow)
            if len(rl_ow.memory) > args.batch_size and episode % args.train_op == 0:
                rl_ow.replay(args.batch_size)

        rl_ow.update_target_model()
        rl_cw.update_target_model()
        if env.simplified_index[-1] != env.N - 1:
            env.boundary(episode, "T")

        train_ratio = env.output(episode, "T", args.err_bounded)
        if episode % args.show_every == 0:
            valid_ratio = run_validation()
            records.append((episode, train_ratio, valid_ratio))
            with log_csv.open("a", newline="", encoding="utf-8") as f:
                writer = csv.writer(f)
                writer.writerow([episode, train_ratio, valid_ratio])
            progress_txt.write_text(
                "\n".join(
                    [
                        f"episode={episode}",
                        f"train_ratio={train_ratio}",
                        f"valid_ratio={valid_ratio}",
                        f"best_valid_ratio={min(best_valid_ratio, valid_ratio)}",
                        f"updated_at={time.strftime('%Y-%m-%d %H:%M:%S')}",
                    ]
                ),
                encoding="utf-8",
            )
            if valid_ratio < best_valid_ratio:
                best_valid_ratio = valid_ratio
                rl_ow.save(str(weights_ow))
                rl_cw.save(str(weights_cw))
            print(f"episode={episode} train_ratio={train_ratio} valid_ratio={valid_ratio}")

    wall_clock = time.time() - wall_start
    run_info.write_text(
        "\n".join(
            [
                "algorithm=MARL4TS",
                "dataset=Geolife",
                f"traj_dir={traj_dir}",
                f"train_amount={args.train_amount}",
                f"valid_amount={args.valid_amount}",
                f"err_bounded={args.err_bounded}",
                f"J={args.J}",
                f"k={args.k}",
                f"D={args.D}",
                f"constraint={args.constraint}",
                f"len_={args.len_}",
                f"show_every={args.show_every}",
                f"batch_size={args.batch_size}",
                f"train_op={args.train_op}",
                f"best_valid_ratio={best_valid_ratio}",
                f"wall_clock_s={wall_clock:.6f}",
                f"weights_ow={weights_ow}",
                f"weights_cw={weights_cw}",
                f"training_log={log_csv}",
                f"progress_txt={progress_txt}",
            ]
        ),
        encoding="utf-8",
    )

    print(f"best_valid_ratio={best_valid_ratio}")
    print(f"weights_ow={weights_ow}")
    print(f"weights_cw={weights_cw}")
    print(f"training_log={log_csv}")


if __name__ == "__main__":
    main()
