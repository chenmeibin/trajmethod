# 轨迹压缩算法实验与源码

本仓库共包含 33 个算法目录，目录中集中存放源码、测试样例和运行脚本，用于运行轨迹压缩程序并比较实验结果。实验以 T-Drive、Geolife、Wuxi、Chengdu、AIS 和 TrajAir 六个数据集为基础；其中采用统一参数设置的算法分别输入 5 组不同的误差阈值或目标压缩比例，通常形成 6 × 5 = 30 组结果，并据此比较压缩率、压缩误差和处理速度。Threshold、Persistence、GraCT 以及 PRESS、TRACE 等使用专用参数的算法，则按照各自参数设置进行测试。仓库覆盖几何误差、时空误差、运动特征、多特征、轨迹编码和路网压缩等方法。

## 代码用途

仓库中的代码主要用于：

1. 在 T-Drive、Geolife、Wuxi、Chengdu、AIS 和 TrajAir 六类轨迹数据上运行不同算法；
2. 对**输入误差阈值**的算法，在相同误差要求下比较压缩率和压缩速度；
3. 对**输入压缩比例**的算法，在相同压缩比例下比较压缩误差和处理速度；
4. 记录压缩率、压缩误差和压缩吞吐量；对于输出为二进制编码流且支持恢复的算法，再统计解压吞吐量。

每个算法目录都包含可用的源码、一个小型输入输出样例，以及用于运行样例的 Shell 脚本。

## 项目结构

```text
algorithms/
├── trajectory_information/
│   ├── trajectory_simplification/
│   │   ├── geometry_error/
│   │   ├── spatiotemporal_error/
│   │   ├── motion_feature/
│   │   └── multi_feature/
│   └── trajectory_coding/
│       ├── reference_coding/
│       └── point_coding/
└── external_information/
    └── road_network_compression/
        ├── PRESS/
        └── TRACE/
```

标准算法目录结构如下：

```text
Algorithm/
├── 源代码
├── sample_input.txt
├── sample_output.txt
└── 运行脚本（.sh）
```

## 算法分类

下面按本仓库实际保留的算法和目录组织方式进行分类。括号中的 `Online` 表示在线算法。

### 一、仅依赖轨迹信息

#### 1. 轨迹简化

##### 几何误差约束压缩

- 当前仓库：Bellman、DP、DPhull、OPW（Online）、CDR（Online）、BQS（Online）、OPERB（Online）
- 主要关注轨迹形状的空间误差，典型误差度量为 PED。

##### 时空误差约束压缩

- 当前仓库：TD-TR、OPW-TR（Online）、STTrace（Online）、SQUISH（Online）、MRPA、DOTS（Online）、CISEDS（Online）、CISEDW（Online）、VOLTCom（Online）、DimWeaver（Online）。
- 在空间误差之外进一步考虑时间同步关系，典型误差度量为 SED。

##### 运动特征保持压缩

- 当前仓库：Angular、Threshold（Online）、DPTS 中的 SP 和 Intersect、Persistence（Online）、Interval（Online）、CASC
- 主要保持方向、速度、转角和运动持续性等特征。

##### 多特征/多特征融合压缩

- 当前仓库：RLTS、MARL4TS（Online）、SEB
- 综合使用空间、时间、运动特征或学习模型进行关键点选择。

#### 2. 轨迹编码

##### 参考编码压缩

- 当前仓库：TrajStore、CFPF、GraCT
- 通过参考轨迹、共享片段、量化或压缩索引降低轨迹集合的存储开销。

##### 点编码压缩

- 当前仓库：Trajic、PILOT-C
- 直接对轨迹点、预测残差或编码序列进行紧凑表示。

### 二、依赖其他外部信息

#### 路网压缩

- 当前仓库：PRESS、TRACE（Online）
- 依赖地图匹配、道路拓扑和路网中的共享路径结构，适用于车辆轨迹。


## 实验设置与比较内容

### 数据集

实验覆盖六类真实轨迹数据：T-Drive、Geolife、Wuxi、Chengdu、AIS 和 TrajAir，涵盖出租车、城市车辆、个人移动、船舶和飞机等对象，并包含不同轨迹规模、平均轨迹长度和采样间隔。实验设计中的数据规模如下：

- T-Drive：约 157 万个点、1000 条轨迹，平均采样间隔约 300 秒；
- Geolife：约 2331 万个点、17784 条轨迹，采样间隔约 1-5 秒；
- Wuxi：约 3970 万个点、15820 条轨迹，采样间隔约 1--3 秒；
- Chengdu：约 534 万个点、20000 条轨迹，采样间隔约 3 秒；
- AIS：约 282 万个点、6000 条轨迹，平均采样间隔约 39 秒；
- TrajAir：约 161 万个点、4000 条轨迹，采样间隔约 1 秒。

### 1. 统一性能指标

- **压缩率**：统一按“压缩后输出文件大小 ÷ 原始输入文件大小”计算；结果越小，表示输出数据相对于输入数据占用的空间越小，压缩程度越高；
- **压缩误差**：根据算法类型分别记录 PED、SED、方向误差或算法自身定义的误差；不同误差度量只在相同指标内进行数值比较；
- **压缩吞吐量（压缩速率）**：以 `MB/s` 表示，计算“原始输入数据大小 ÷ 压缩耗时”；同时保留总压缩时间和单位点处理时间；
- **解压吞吐量**：对输出为二进制编码流、并提供解码或恢复过程的算法，计算“解压得到的数据大小 ÷ 解压耗时”，同样以 `MB/s` 表示。

### 2. 两类核心实验

- **输入误差阈值的实验（min-#）**：对 DP、DPhull、OPW、CDR、BQS、OPERB，以及部分时空误差、运动特征和编码方法，输入算法能够接受的误差参数；在相同误差要求下比较压缩率，并同时记录压缩速度。统一实验中使用的代表性误差值为 `1e-5、5e-5、1e-4、5e-4、1e-3`，但这组值不适用于所有算法。
- **输入压缩比例的实验（min-ε）**：对支持按比例或保留预算运行的算法，输入 `2、5、10、20、50`，即目标比例 `0.5、0.2、0.1、0.05、0.02`（分别对应 `50%、20%、10%、5%、2%`）；在相同压缩比例下比较压缩误差，并记录处理速度。Bellman、STTrace、SQUISH 以及部分学习型方法属于这一类，具体以各目录的运行脚本为准。

需要注意，算法的输入并不统一：Threshold 以速度、方向等阈值为主，Persistence 使用距离和持续性参数，GraCT 使用快照间隔，PRESS/TRACE 使用路网参数；这些方法不能直接套用上面的五个误差值或五个比例值，而是按照各自脚本中的参数进行实验。

### 3. 各类算法比较

- **几何误差方法**：输入误差阈值的 DP、DPhull、OPW、CDR、BQS 和 OPERB，在相同误差下比较压缩率和吞吐量；输入压缩比例的 Bellman，在相同压缩比例下比较 PED 和处理速度。
- **时空误差方法**：输入误差阈值的 TD-TR、MRPA、OPW-TR、DOTS、CISEDS、CISEDW、VOLTCom 和 DimWeaver，在相同 SED 约束下比较压缩率和吞吐量；输入压缩比例的 SQUISH 和 STTrace，在相同压缩比例下比较 SED 和处理速度。
- **运动特征方法**：输入误差阈值的 SP、Intersect 和 Interval，在相同误差要求下比较压缩率和特征误差；Angular、CASC、Persistence、Threshold 使用方向、速度、距离等专用参数，按各自参数比较特征保持效果和处理速度。
- **多特征方法**：RLTS、MARL4TS 和 SEB 按目标保留比例或误差约束运行，在相同目标约束下比较压缩误差、压缩率和模型处理效率。
- **参考编码和点编码方法**：TrajStore、CFPF、Trajic 和 PILOT-C 主要输入误差阈值，在相同误差下比较编码规模、压缩率和吞吐量；GraCT 使用快照间隔等专用参数，单独比较编码规模和处理效率。
- **路网方法**：PRESS 和 TRACE 使用各自的路网参数，单独比较不同车辆轨迹数据集下的存储开销和处理效率，不与自由空间轨迹简化方法直接混合排名。

## Requirements

### 操作系统

- Linux、macOS 或 Windows 10/11；
- Windows 用户建议使用 Git Bash 或 WSL 运行 `.sh` 脚本。

### C/C++ 编译环境

- 支持 C++17 的编译器：GCC 9+、Clang 10+ 或 Apple Clang 15+；
- Windows 可使用 MinGW-w64、WSL 中的 GCC，或其他兼容 C++17 的工具链；
- 使用 CMake 的算法（如 GraCT、DimWeaver）需要 CMake 3.15 或更高版本；
- 使用 Makefile 的算法需要 GNU Make；
- 所有算法不要求统一的 IDE，编译命令已经写在各自目录的 `.sh` 脚本中。

### Shell 与 Python 环境

- Bash 环境，用于执行算法目录中的 `.sh` 脚本；
- Python 3.8+，用于 RLTS、MARL4TS、SEB 等包含 Python 代码的学习型算法；
- Python 依赖以各算法目录中的 `requirements.txt` 或 README 说明为准，不同算法的 TensorFlow、Keras 等版本可能不同。

## 快速开始

大多数算法都可以通过所在目录中的 Shell 脚本运行。例如：

```bash
cd algorithms/trajectory_information/trajectory_simplification/geometry_error/DP
bash DP.sh
```

使用 `run.sh` 脚本的算法示例：

```bash
cd algorithms/trajectory_information/trajectory_coding/point_coding/trajic
bash run.sh
```

在 Linux 或 macOS 中，如果脚本没有执行权限，可以先运行：

```bash
chmod +x *.sh
./DP.sh
```

在 Windows 中，请使用 Git Bash 或 WSL 运行 `.sh` 脚本。脚本会使用对应目录中的
`sample_input.txt`，并在算法支持时生成 `sample_output.txt`。

## 输入输出样例

测试文件较小，主要用于说明算法的输入输出格式：

- `sample_input.txt`：输入轨迹或编码数据；
- `sample_output.txt`：样例运行生成的输出结果。

不同算法的具体格式可能不同。部分方法还需要额外的配置文件或路网数据，相关命令和准备步骤写在对应的 Shell 脚本中。

## 特殊说明

- **DPTS**：运行 `DPTS.sh` 前，需要在配置文件中选择批处理简化方法。
- **Trajic、TrajStore 和 GraCT**：需要相应的构建工具；Trajic 使用 Make 和 Boost，GraCT 使用 CMake。
- **PRESS 和 TRACE**：依赖路网数据或预处理步骤，目录中保留了最小测试样例。
- **CFPF**：完整流程可能需要额外的特征点预处理，仓库中提供了基本样例。
- **MARL4TS、RLTS 和 SEB**：学习型算法完整训练时可能需要额外的 Python 包和轨迹数据，仓库中的样例用于检查代码和输入输出格式。

## Python 依赖

机器学习算法需要 Python 环境。不同算法的原始实验环境可能存在版本差异，请根据实际运行的算法安装依赖。

常用依赖包括：

```bash
pip install numpy pandas matplotlib tensorflow keras sortedcontainers
```

## 可复现运行

进入任意算法目录，查看其中的 `.sh` 脚本，并使用附带的输入样例运行。每个算法的源码、输入样例、输出样例和运行脚本放在同一目录中，便于独立检查和复现实验。
