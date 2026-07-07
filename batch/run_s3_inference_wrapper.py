from __future__ import annotations

import argparse
import csv
import importlib.machinery
import importlib.util
import os
import pickle
import sys
import time
import types
from collections import defaultdict
from pathlib import Path

import torch


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run S3 inference with local dataset/checkpoint shims, without editing S3 source files."
    )
    parser.add_argument("--workspace-root", default=r"D:\all_traj_method")
    parser.add_argument("--dataset", default="tdrive", choices=["tdrive", "geolife"])
    parser.add_argument("--checkpoint", default="s3_tdrive_local")
    parser.add_argument("--src-file", default="eval.src")
    parser.add_argument("--tag", default="eval_ratio_0.2")
    return parser.parse_args()


def install_stub_modules() -> None:
    if importlib.util.find_spec("tabulate") is None and "tabulate" not in sys.modules:
        tabulate_mod = types.ModuleType("tabulate")
        tabulate_mod.__spec__ = importlib.machinery.ModuleSpec("tabulate", loader=None)
        tabulate_mod.tabulate = lambda rows, *args, **kwargs: str(rows)
        sys.modules["tabulate"] = tabulate_mod

    if "wandb" not in sys.modules:
        wandb = types.ModuleType("wandb")
        wandb.__spec__ = importlib.machinery.ModuleSpec("wandb", loader=None)
        wandb.login = lambda *args, **kwargs: None
        wandb.init = lambda *args, **kwargs: None
        wandb.watch = lambda *args, **kwargs: None
        wandb.log = lambda *args, **kwargs: None
        wandb.finish = lambda *args, **kwargs: None
        sys.modules["wandb"] = wandb

    if "apex" not in sys.modules:
        apex = types.ModuleType("apex")
        apex.__spec__ = importlib.machinery.ModuleSpec("apex", loader=None)
        amp = types.SimpleNamespace(initialize=lambda model, optimizer, opt_level=None: (model, optimizer))
        apex.amp = amp
        sys.modules["apex"] = apex

    if importlib.util.find_spec("visdom") is None and "visdom" not in sys.modules:
        visdom = types.ModuleType("visdom")
        visdom.__spec__ = importlib.machinery.ModuleSpec("visdom", loader=None)

        class DummyVisdom:
            def __init__(self, *args, **kwargs):
                pass

        visdom.Visdom = DummyVisdom
        sys.modules["visdom"] = visdom

    if importlib.util.find_spec("seaborn") is None and "seaborn" not in sys.modules:
        seaborn = types.ModuleType("seaborn")
        seaborn.__spec__ = importlib.machinery.ModuleSpec("seaborn", loader=None)
        seaborn.set = lambda *args, **kwargs: None
        seaborn.heatmap = lambda *args, **kwargs: None
        sys.modules["seaborn"] = seaborn

    if importlib.util.find_spec("glob2") is None and "glob2" not in sys.modules:
        import glob as std_glob

        glob2 = types.ModuleType("glob2")
        glob2.__spec__ = importlib.machinery.ModuleSpec("glob2", loader=None)
        glob2.glob = std_glob.glob
        sys.modules["glob2"] = glob2

    if importlib.util.find_spec("sentencepiece") is None and "sentencepiece" not in sys.modules:
        sentencepiece = types.ModuleType("sentencepiece")
        sentencepiece.__spec__ = importlib.machinery.ModuleSpec("sentencepiece", loader=None)

        class DummySentencePieceProcessor:
            def __init__(self, *args, **kwargs):
                pass

        sentencepiece.SentencePieceProcessor = DummySentencePieceProcessor
        sys.modules["sentencepiece"] = sentencepiece

    if importlib.util.find_spec("gensim") is None and "gensim" not in sys.modules:
        gensim = types.ModuleType("gensim")
        gensim.__spec__ = importlib.machinery.ModuleSpec("gensim", loader=None)
        gensim_models = types.ModuleType("gensim.models")
        gensim_models.__spec__ = importlib.machinery.ModuleSpec("gensim.models", loader=None)

        class DummyFastText:
            pass

        gensim_models.FastText = DummyFastText
        gensim.models = gensim_models
        sys.modules["gensim"] = gensim
        sys.modules["gensim.models"] = gensim_models

    if importlib.util.find_spec("rouge") is None and "rouge" not in sys.modules:
        rouge = types.ModuleType("rouge")
        rouge.__spec__ = importlib.machinery.ModuleSpec("rouge", loader=None)

        class DummyRouge:
            def get_scores(self, *args, **kwargs):
                return []

        rouge.Rouge = DummyRouge
        sys.modules["rouge"] = rouge

    if importlib.util.find_spec("umap") is None and "umap" not in sys.modules:
        umap = types.ModuleType("umap")
        umap.__spec__ = importlib.machinery.ModuleSpec("umap", loader=None)
        umap.UMAP = object
        sys.modules["umap"] = umap

    if importlib.util.find_spec("graphviz") is None and "graphviz" not in sys.modules:
        graphviz = types.ModuleType("graphviz")
        graphviz.__spec__ = importlib.machinery.ModuleSpec("graphviz", loader=None)

        class DummyDigraph:
            def __init__(self, *args, **kwargs):
                pass

        graphviz.Digraph = DummyDigraph
        sys.modules["graphviz"] = graphviz

    if importlib.util.find_spec("funcy") is None and "funcy" not in sys.modules:
        funcy = types.ModuleType("funcy")
        funcy.__spec__ = importlib.machinery.ModuleSpec("funcy", loader=None)

        def merge(*dicts):
            result = {}
            for d in dicts:
                if isinstance(d, dict):
                    result.update(d)
            return result

        funcy.merge = merge
        sys.modules["funcy"] = funcy


def safe_load_checkpoint(name: str, path: str):
    import utils.training as training_mod  # type: ignore

    model_fname = os.path.join(path, f"{name}.pt")
    with open(model_fname, "rb") as f:
        return torch.load(f, map_location="cpu", weights_only=False)


def main() -> None:
    args = parse_args()
    workspace_root = Path(args.workspace_root).resolve()
    s3_root = workspace_root / "S3"
    dataset_dir = s3_root / "datasets" / args.dataset
    checkpoint_dir = s3_root / "checkpoints"
    src_file = dataset_dir / args.src_file
    output_dir = workspace_root / "test" / f"S3_test_{args.dataset}_checkpoint_{args.checkpoint}_{args.tag}"
    output_dir.mkdir(parents=True, exist_ok=True)
    os.environ["MPLCONFIGDIR"] = str((workspace_root / "tmp" / "mplconfig_s3_infer").resolve())
    Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)

    install_stub_modules()

    sys.path.insert(0, str(s3_root))
    sys.path.insert(0, str(s3_root / "models"))
    os.chdir(s3_root)

    import numpy as np
    import sys_config  # type: ignore
    import utils.config as config_mod  # type: ignore
    import modules.data.utils as data_utils  # type: ignore
    import utils.gcn_emb as gcn_emb_mod  # type: ignore
    import mylogger.helpers as logger_helpers  # type: ignore
    import utils.training as training_mod  # type: ignore
    from modules.data.datasets import AEDataset  # type: ignore
    from modules.data.collates import Seq2SeqOOVCollate  # type: ignore
    from torch.utils.data import DataLoader
    import modules.models as models_mod  # type: ignore
    from generate.utils import id2txt  # type: ignore

    sys_config.DATA_DIR = str(dataset_dir)
    config_mod.DATA_DIR = str(dataset_dir)
    data_utils.wc = lambda filename: sum(1 for _ in open(filename, "r", encoding="utf-8", errors="ignore"))
    gcn_emb_mod.gcn_emb = lambda *a, **k: np.zeros((1, 1), dtype=np.float32)
    models_mod.gcn_emb = lambda *a, **k: np.zeros((1, 1), dtype=np.float32)

    def safe_files_to_dict(dirs, safe=True):
        import glob as std_glob

        file_data = defaultdict(dict)
        for dir_ in dirs:
            for file in std_glob.glob(os.path.join(dir_, "*.py")):
                dirname = os.path.split(dir_)[1]
                filename = os.path.basename(file)
                if safe:
                    filename = filename.replace(".", "[dot]")
                file_data[dirname][filename] = Path(file).read_text(encoding="utf-8", errors="ignore")
        return file_data

    logger_helpers.files_to_dict = safe_files_to_dict
    training_mod.load_checkpoint = safe_load_checkpoint

    checkpoint = training_mod.load_checkpoint(args.checkpoint, str(checkpoint_dir))
    config = checkpoint["config"]
    vocab = checkpoint["vocab"]
    config["data"]["seq_len"] = 1024

    with (dataset_dir / "pickle.txt").open("rb") as f:
        region = pickle.loads(pickle.load(f))

    def giga_tokenizer(x: str):
        return x.strip().lower().split()

    dataset = AEDataset(
        str(src_file),
        preprocess=giga_tokenizer,
        vocab=vocab,
        seq_len=config["data"]["seq_len"],
        return_oov=True,
        oovs=config["data"]["oovs"],
    )
    data_loader = DataLoader(
        dataset,
        batch_size=config["batch_size"],
        num_workers=0,
        collate_fn=Seq2SeqOOVCollate(),
    )

    model = models_mod.Seq2Seq2Seq(len(dataset.vocab), **config["model"]).to("cpu")
    model.load_state_dict(checkpoint["model"])
    model.eval()

    output_txt = output_dir / "s3_tdrive_compressed.txt"
    summary_csv = output_dir / "s3_tdrive_metrics_summary.csv"
    run_info = output_dir / "run_info.txt"

    total_input_points = 0
    total_output_points = 0
    processed_trajs = 0

    wall_start = time.time()
    with output_txt.open("w", encoding="utf-8", newline="\n") as out, torch.no_grad():
        for batch in data_loader:
            batch_oov_map = batch[-1]
            batch = batch[:-1]
            batch = [x.to("cpu") for x in batch]
            inp_src, out_src, inp_trg, out_trg, src_lengths, trg_lengths = batch

            max_ratio = 0.2
            trg_lengths = torch.clamp(src_lengths * max_ratio, min=9, max=25)
            trg_lengths = torch.floor(trg_lengths)

            mask_zeros = torch.zeros(inp_src.size(0), vocab.size).to(inp_src)
            mask_matrix = mask_zeros.scatter(1, inp_src, 1)

            _, dec1, _, _ = model(
                inp_src,
                inp_trg,
                src_lengths,
                trg_lengths,
                sampling=0,
                mask_matrix=mask_matrix,
                vocab=vocab,
                region=region,
            )

            comp = dec1[3].max(-1)[1].tolist()
            src = inp_src.tolist()
            for c, s, src_len, trg_len, oov_map in zip(comp, src, src_lengths.tolist(), trg_lengths.tolist(), batch_oov_map):
                trg_len = int(trg_len)
                c = c[:trg_len]
                try:
                    c.sort(key=lambda j: s.index(j))
                except ValueError:
                    pass

                latent = id2txt(torch.tensor([c]), vocab, [oov_map] if oov_map is not None else None)
                line = latent[0].strip()
                out.write(line + "\n")

                total_input_points += int(src_len)
                total_output_points += len([tok for tok in line.split() if tok and tok != "UNK"])
                processed_trajs += 1

    wall_clock_s = time.time() - wall_start
    keep_ratio = (total_output_points / total_input_points) if total_input_points else 0.0
    saved_ratio = 1.0 - keep_ratio

    with summary_csv.open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "algorithm",
                "implementation",
                "dataset",
                "checkpoint",
                "src_file",
                "compression_ratio_setting",
                "processed_trajs",
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
                "S3",
                "python_original_with_wrapper",
                args.dataset,
                args.checkpoint,
                args.src_file,
                "0.2_with_min9_max25",
                processed_trajs,
                total_input_points,
                total_output_points,
                f"{keep_ratio:.6f}",
                f"{saved_ratio:.6f}",
                f"{(1.0 / keep_ratio) if keep_ratio else 0.0:.6f}",
                f"{wall_clock_s:.6f}",
            ]
        )

    run_info.write_text(
        "\n".join(
            [
                "algorithm=S3",
                "implementation=python_original_with_wrapper",
                f"dataset={args.dataset}",
                f"checkpoint={args.checkpoint}",
                f"src_file={src_file}",
                "compression_ratio_setting=0.2_with_min9_max25",
                f"processed_trajs={processed_trajs}",
                f"total_input_points={total_input_points}",
                f"total_output_points={total_output_points}",
                f"keep_ratio={keep_ratio:.6f}",
                f"saved_ratio={saved_ratio:.6f}",
                f"overall_compression_ratio={(1.0 / keep_ratio) if keep_ratio else 0.0:.6f}",
                f"wall_clock_s={wall_clock_s:.6f}",
                f"output_txt={output_txt}",
                f"summary_csv={summary_csv}",
            ]
        ),
        encoding="utf-8",
    )

    print(f"processed_trajs={processed_trajs}")
    print(f"total_input_points={total_input_points}")
    print(f"total_output_points={total_output_points}")
    print(f"summary_csv={summary_csv}")


if __name__ == "__main__":
    main()
