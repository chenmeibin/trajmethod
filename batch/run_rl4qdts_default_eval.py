import argparse
import csv
import os
import pathlib
import pickle
import sys
import time


def parse_args():
    parser = argparse.ArgumentParser(description="Run RL4QDTS default-like evaluation on Geolife.")
    parser.add_argument("--workspace-root", required=True)
    parser.add_argument("--tag", default="default")
    return parser.parse_args()


def count_points(db):
    return sum(len(traj) for traj in db)


def main():
    args = parse_args()
    sys.argv = [sys.argv[0]]
    root = pathlib.Path(args.workspace_root)
    rl4qdts_dir = root / "RL4QDTS"
    geolife_dir = rl4qdts_dir / "TrajData" / "Geolife_out"
    test_dir = root / "test" / f"RL4QDTS_test_Geolife_{args.tag}"
    test_dir.mkdir(parents=True, exist_ok=True)

    julia_bin = pathlib.Path(r"D:\julia\Julia-1.12.6\bin")
    os.environ["PATH"] = str(julia_bin) + os.pathsep + os.environ.get("PATH", "")

    sys.path.insert(0, str(rl4qdts_dir))
    os.chdir(rl4qdts_dir)

    import rl_brain  # noqa: WPS433
    import rl_env  # noqa: WPS433

    total_trajs = len([p for p in geolife_dir.iterdir() if p.is_file() and p.name.isdigit()])
    if total_trajs == 0:
        raise RuntimeError(f"No prepared trajectories found in {geolife_dir}")

    valid_db_amount = min(1000, total_trajs)
    val_start = 0
    ratio = 0.0025
    k = 2
    length = 2

    results = []

    for label, level_start, level_end in [("data", 9, 12), ("gau", 10, 12)]:
        agent_sel = rl_brain.AGENT_Sel(k * length, k)
        agent_sam = rl_brain.AGENT_Sam(16, 9)
        agent_sel.load(str(rl4qdts_dir / "save" / f"model_{label}_RL_INB_F1.h5"))
        agent_sam.load(str(rl4qdts_dir / "save" / f"model_{label}_RL_OUB_F1.h5"))

        env_sel = rl_env.TrajComp("./TrajData/Geolife_out/", val_start, val_start + valid_db_amount, k, length)
        val_set = env_sel.ori_traj_set
        env_sam = rl_env.TrajSam(val_set, level_start, label)
        sam_size = int(env_sel.DB_size * ratio)

        start = time.time()
        sample_list = []
        for _ in range(sam_size):
            observation_oub = env_sam.starting(level_start)
            while True:
                action_oub = agent_sam.fast_online_act(observation_oub)
                if action_oub == 8 or env_sam.level_cur == level_end:
                    break
                observation_oub = env_sam.get_agent_oub_state(action_oub)
            sample_list.append(env_sam.cubeID)
        env_sam.get_ID2Grids(sample_list)

        env_sel_v = rl_env.TrajComp(
            "./TrajData/Geolife_out/",
            val_start,
            val_start + valid_db_amount,
            k,
            length,
            "",
            env_sam.ID2Grids,
            "V",
        )
        buffer_valid_size = int(ratio * env_sel_v.DB_size) + 2 * valid_db_amount
        for traj_id in range(valid_db_amount):
            steps = len(env_sel_v.ori_traj_set[traj_id])
            start_id = (traj_id, 0)
            end_id = (traj_id, steps - 1)
            env_sel_v.read(start_id, end_id)
            env_sel_v.add_buffer(start_id, end_id)

        epoch_i_c = 0
        while len(env_sel_v.F_ward) < buffer_valid_size:
            observation_inb = env_sel_v.get_agent_inb_state(sample_list[epoch_i_c])
            action_inb = agent_sel.fast_online_act(observation_inb)
            env_sel_v.update_inb(action_inb)
            epoch_i_c += 1

        sim_db = env_sel_v.get_simplified_DB()
        wall_clock = time.time() - start

        output_pickle = test_dir / f"rl4qdts_{label}_sim_db.pkl"
        with output_pickle.open("wb") as f:
            pickle.dump(sim_db, f, protocol=2)

        total_input_points = count_points(val_set)
        total_output_points = count_points(sim_db)
        keep_ratio = total_output_points / total_input_points if total_input_points else 0.0
        saved_ratio = 1.0 - keep_ratio
        compression_ratio = total_input_points / total_output_points if total_output_points else 0.0

        results.append(
            {
                "algorithm": "RL4QDTS",
                "label": label,
                "dataset": "Geolife",
                "ratio": f"{ratio}",
                "processed_trajs": str(valid_db_amount),
                "level_start": str(level_start),
                "level_end": str(level_end),
                "total_input_points": str(total_input_points),
                "total_output_points": str(total_output_points),
                "keep_ratio": f"{keep_ratio:.6f}",
                "saved_ratio": f"{saved_ratio:.6f}",
                "overall_compression_ratio": f"{compression_ratio:.6f}",
                "wall_clock_s": f"{wall_clock:.6f}",
                "output_pickle": str(output_pickle),
            }
        )

    summary_csv = test_dir / "rl4qdts_geolife_metrics_summary.csv"
    with summary_csv.open("w", encoding="utf-8", newline="") as f:
        writer = csv.DictWriter(f, fieldnames=list(results[0].keys()))
        writer.writeheader()
        writer.writerows(results)

    run_info = test_dir / "run_info.txt"
    with run_info.open("w", encoding="utf-8") as f:
        f.write("algorithm=RL4QDTS\n")
        f.write("dataset=Geolife\n")
        f.write("mode=default_like\n")
        f.write("note=Kept default model files, ratio, K, LEN, and level settings; adapted trajectory slice to full available Geolife_out because repository only contains 242 source trajectories.\n")
        f.write(f"prepared_trajs={total_trajs}\n")
        f.write(f"processed_trajs={valid_db_amount}\n")
        f.write(f"summary_csv={summary_csv}\n")

    print(f"processed_trajs={valid_db_amount}")
    print(f"summary_csv={summary_csv}")


if __name__ == "__main__":
    main()
