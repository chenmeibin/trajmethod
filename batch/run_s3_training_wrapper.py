from __future__ import annotations

import argparse
import importlib.util
import importlib.machinery
import os
import runpy
import sys
import types
from pathlib import Path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run S3 training with local dataset/config shims, without editing S3 source files."
    )
    parser.add_argument("--workspace-root", default=r"D:\all_traj_method")
    parser.add_argument("--dataset", default="tdrive", choices=["tdrive", "geolife"])
    parser.add_argument("--config", default="batch/s3_tdrive_train.yaml")
    parser.add_argument("--name", default="s3_tdrive_local")
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

            def line(self, *args, **kwargs):
                return None

            def text(self, *args, **kwargs):
                return None

            def scatter(self, *args, **kwargs):
                return None

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


def main() -> None:
    args = parse_args()
    workspace_root = Path(args.workspace_root).resolve()
    s3_root = workspace_root / "S3"
    dataset_dir = s3_root / "datasets" / args.dataset
    config_path = (workspace_root / args.config).resolve()

    install_stub_modules()

    sys.path.insert(0, str(s3_root))
    sys.path.insert(0, str(s3_root / "models"))
    os.environ["MPLCONFIGDIR"] = str((workspace_root / "tmp" / "mplconfig_s3_train").resolve())
    Path(os.environ["MPLCONFIGDIR"]).mkdir(parents=True, exist_ok=True)
    os.chdir(s3_root)

    import sys_config  # type: ignore
    import utils.config as config_mod  # type: ignore
    import modules.data.utils as data_utils  # type: ignore
    import utils.gcn_emb as gcn_emb_mod  # type: ignore
    import mylogger.helpers as logger_helpers  # type: ignore
    import numpy as np
    from collections import defaultdict
    from pathlib import Path as StdPath
    import glob as std_glob

    sys_config.DATA_DIR = str(dataset_dir)
    config_mod.DATA_DIR = str(dataset_dir)
    data_utils.wc = lambda filename: sum(1 for _ in open(filename, "r", encoding="utf-8", errors="ignore"))
    gcn_emb_mod.gcn_emb = lambda *args, **kwargs: np.zeros((1, 1), dtype=np.float32)

    def safe_files_to_dict(dirs, safe=True):
        file_data = defaultdict(dict)
        for dir_ in dirs:
            for file in std_glob.glob(os.path.join(dir_, "*.py")):
                dirname = os.path.split(dir_)[1]
                filename = os.path.basename(file)
                if safe:
                    filename = filename.replace(".", "[dot]")
                file_data[dirname][filename] = StdPath(file).read_text(encoding="utf-8", errors="ignore")
        return file_data

    logger_helpers.files_to_dict = safe_files_to_dict

    sys.argv = [
        str(s3_root / "models" / "s3.py"),
        "--config",
        str(config_path),
        "--device",
        "cpu",
        "--name",
        args.name,
    ]

    runpy.run_path(str(s3_root / "models" / "s3.py"), run_name="__main__")


if __name__ == "__main__":
    main()
