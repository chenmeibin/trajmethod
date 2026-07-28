# traj-compression

本仓库整理了轨迹压缩与轨迹简化算法源码，用于不同误差模型、运行模式和编码方法的实验对比。

每个算法目录主要包含：

- 算法源码
- `sample_input.txt`
- `sample_output.txt`
- 一个 `.sh` 运行脚本

## 几何误差算法

- Bellman
- BQS
- CDR
- DP
- DPhull
- OPERB
- OPW

## 时空误差算法

- DimWeaver
- DOTS
- MRPA
- OPW-TR
- SQUISH
- STTrace
- TD-TR
- VOLTCom

## 运动特征算法

- Angular
- CASC
- DPTS（包含 DP、SP、Intersect、Split、Merge、Greedy 等选项）
- Interval
- Persistence
- Threshold

## 多特征与学习型算法

- MARL4TS
- RLTS
- SEB

## 路网压缩算法

- PRESS
- TRACE

## 参考编码与索引算法

- CFPF
- GraCT
- TrajStore

## 点级编码算法

- PILOT-C
- Trajic

## 使用方法

### 1. 普通 C/C++ 算法

进入算法目录，给运行脚本增加执行权限，然后运行：

```bash
cd algorithms/geometry_error/DP
chmod +x DP.sh
./DP.sh
```

其他算法使用对应目录中的 `.sh` 文件，脚本会编译源码，并使用 `sample_input.txt`
生成 `sample_output.txt`。

### 2. DPTS

DPTS 包含多种批处理算法，算法编号写在 `config.txt` 中：

```bash
cd algorithms/motion_feature/DPTS
chmod +x DPTS.sh
./DPTS.sh
```

常用编号包括：

- `0`：DP
- `2`：SP
- `6`：Intersect
- `7`：Split
- `8`：Merge
- `9`：Greedy

### 3. Trajic、TrajStore 和 GraCT

Trajic：

```bash
cd algorithms/point_coding/trajic
chmod +x run.sh
./run.sh
```

TrajStore：

```bash
cd algorithms/reference_coding/trajstore
chmod +x run.sh
./run.sh
```

GraCT：

```bash
cd algorithms/reference_coding/gract
chmod +x run.sh
./run.sh
```

Trajic 需要 Make 和 Boost，GraCT 需要 CMake。

### 4. PRESS、TRACE 和 CFPF

PRESS 和 TRACE 需要路网数据及相应的服务或可执行程序。CFPF 需要 SEB 生成的特征点
和预处理轨迹。目录中的样例主要用于说明输入输出格式。

### 5. 强化学习算法

MARL4TS 推荐使用 Python 3.6、TensorFlow 1.8 和 Keras 2.2：

```bash
pip install tensorflow==1.8.0 keras==2.2.0 numpy pandas matplotlib
```

RLTS 推荐使用 Python 3.6 和 TensorFlow 1.8：

```bash
pip install tensorflow==1.8.0 numpy matplotlib sortedcontainers
```

进入对应目录后执行 `run.sh`。完整训练需要准备轨迹数据集。

### 6. PILOT-C Python 依赖

```bash
pip install -r algorithms/point_coding/PILOT-C/requirements.txt
```

PILOT-C 的 C++ 示例可以直接运行目录中的 `run.sh`。
