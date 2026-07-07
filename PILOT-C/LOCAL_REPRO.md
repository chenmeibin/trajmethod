# PILOT-C Local Repro

这个仓库原始代码基本按作者本机环境编写，直接运行会依赖固定的 Linux 路径。当前目录已经补了一层本地适配，适合在 `D:\all_traj_method\PILOT-C` 里做复现。

## 目录约定

- 数据默认放在 `PILOT-C/workspace/datasets`
- 缓存默认放在 `PILOT-C/workspace/cache`
- 结果默认放在 `PILOT-C/workspace/result`

这些目录也可以通过环境变量覆盖：

- `PRESS_DATA_ROOT`
- `PRESS_CACHE_ROOT`
- `PRESS_RESULT_ROOT`

如果你要预处理原始公开数据，还可以设置：

- `GEOLIFE_RAW_ROOT`
- `MOPSI_RAW_ROOT`
- `NUPLAN_DATA_ROOT`
- `SHANGQI_DATA_ROOT`

## 依赖安装

```powershell
py -m pip install -r requirements.txt
```

## 最小运行方式

对 `geolife` / `mopsi` / `test_scalability` 这几类二维轨迹，输入文件格式都是纯文本，每行一条点：

```text
timestamp,x,y
```

例如：

```text
0,0,0
1,1.2,0.4
2,2.0,1.1
```

运行命令：

```powershell
py run_pilotc_local.py --input .\workspace\datasets\test_scalability\traj_demo.txt --data-source test_scalability
```

如果你要跑 `nuplan`，输入应当是一个包含 `ego.csv` 的目录：

```powershell
py run_pilotc_local.py --input D:\path\to\scene_dir --data-source nuplan
```

## 说明

- `run_pilotc_local.py` 会同时跑 `SQUISH-E`、`CISED-S`、`CISED-W` 和 `PILOT-C`
- 指标会输出到 `workspace/result/.../metrics.csv`
- 原始论文脚本 `no_map_compress.py` 仍然保留，当前改动主要是把硬编码路径换成了本地可配置路径
