# dualDie_readme

本 README 面向 `pipeline/case/dualDie` 双 DIE 并行演示用例，帮助你：
- 快速理解本 case 的整体功能与数据流拓扑（每个 DIE 独立跑一套多路 AI 流水线）。
- 参考现有实现，按需定制为你的业务场景（通道分配、亲和绑定、性能调优）。
- 正确配置与运行，复现运行效果。

关联文件位置：
- 启停脚本：`pipeline/case/dualDie/start_pipeline.sh`、`pipeline/case/dualDie/stop_pipeline.sh`
- 分 DIE 脚本：`pipeline/case/dualDie/die0/die0.sh`、`pipeline/case/dualDie/die1/die1.sh`
- 配置目录：`pipeline/case/dualDie/die0/config/*.yaml`、`pipeline/case/dualDie/die1/config/*.yaml`

---

## 案例整体功能描述

本案例在同一设备的两个 DIE 上分别运行各自的多路目标检测可视化流水线，演示“多 DIE 并行推理+显示”的能力。每个 DIE 内部均包括“解复用与解码 → 预处理 → 推理 → 后处理/跟踪 → OSD → 多画面拼接 → 显示”的完整链路；通过 `numactl --membind/--cpunodebind` 与元素 `-die` 亲和实现计算与内存的本地化，最大化多 DIE 的并行吞吐。`start_pipeline.sh` 负责设置必要的时钟/寄存器并按顺序启动 `die1` 与 `die0` 两条流水线。

---

## 1. 功能与数据流拓扑

以单个 DIE 的链路为例（`die0.sh`/`die1.sh` 结构一致，`die1` 增加了元素 `-die 1` 亲和）：

```
EsAvDemux xN -> EsVdec xN ---> EsMux("mux1") -> EsQueue -> EsPreProcess -> EsInfer -> EsQueue -> EsPostProcess -> EsTracker -> EsOsd -> EsQueue -> EsVideoGrid -> EsQueue -> EsVideoSink
```

- EsAvDemux：读取该 DIE 分配的多路输入（文件/IPC 等），输出压缩帧。
- EsVdec：将压缩帧解码为图像帧（如 NV12）。
- EsMux：将多路解码输出汇聚为统一输出流，保证时序推进。
- EsQueue：上下游解耦缓冲，提高稳态吞吐。
- EsPreProcess：缩放/色彩空间转换/归一化，准备推理输入。
- EsInfer：执行检测模型推理（NPU），可结合 `isAsync`、输出池等参数调优。
- EsPostProcess：阈值过滤、NMS，得到结构化检测结果。
- EsTracker：可选跟踪，稳定 ID 与轨迹（如 `EsTracker.yaml`）。
- EsOsd：在帧上叠加检测框/标签等信息。
- EsVideoGrid：将多路画面拼接（网格布局），用于总览显示。
- EsVideoSink：将结果输出至显示设备（如 HDMI）。

双 DIE 并行（抽象示意）：

```
DIE0: [Demux/Decode xN] -> Mux0 -> Preproc0 -> Infer0 -> Post0 -> Track0 -> OSD0 -> Grid0 -> VO0
DIE1: [Demux/Decode xN] -> Mux1 -> Preproc1 -> Infer1 -> Post1 -> Track1 -> OSD1 -> Grid1 -> VO1
  ^ numactl 绑定 node0           ^ numactl 绑定 node1
  ^ 元素默认 -die 0              ^ 元素显式 -die 1
```

---

## 2. 运行方法

方式 A：一键启停（推荐）

```bash
# 设备端 Linux shell
sh pipeline/case/dualDie/start_pipeline.sh
# 结束运行
sh pipeline/case/dualDie/stop_pipeline.sh
```

- `start_pipeline.sh` 会：
  - 使用 `tools/devmem` 设置相关寄存器（如 NPU 时钟），示例：`0x51828180/0x5182817c`、`0x71828180/0x7182817c`；
  - 切换到 `die1` 目录，清空 VB 后后台启动 `die1.sh`；
  - 等待 10 秒，切换到 `die0` 目录后台启动 `die0.sh`。

方式 B：分别启动各 DIE（便于调试）

```bash
# DIE1（node1）
cd pipeline/case/dualDie/die1
sh die1.sh

# DIE0（node0）
cd pipeline/case/dualDie/die0
sh die0.sh
```

脚本关键信息：
- `die0.sh`：`numactl --membind=0 --cpunodebind=0`；每路 `EsAvDemux+EsVdec` 后并入 `EsMux -name mux1`；随后 `EsPreProcess -> EsInfer -> EsPostProcess -> EsTracker -> EsOsd -> EsVideoGrid -> EsVideoSink`。
- `die1.sh`：`numactl --membind=1 --cpunodebind=1`，并为元素追加 `-die 1`；链路与 `die0` 类似。注意：脚本中 `espl_lanuch` 疑为拼写错误，实际应为 `espl_launch`，请按平台实际可执行文件修正。
- 两侧均设置 `PL_LOG_LEVEL=4`，放开 `ulimit` 限制，`cloopnum=200000000` 近似“无穷循环”。

---

## 3. 配置文件总览与要点

- 目录结构：
  - `die0/config/*.yaml`、`die1/config/*.yaml` 两套配置彼此独立，通常同构但可按需差异化。
- 输入与解码：
  - `EsAvDemux_*.yaml`：通道输入（文件/IPC）；
  - `EsVdec.yaml`：解码像素/输出等参数。
- 前处理/推理/后处理/跟踪：
  - `EsPreProcess.yaml`：尺寸/像素/归一化；
  - `EsInfer.yaml`：`model-filepath`、`isAsync`（而非 `inferType`）、`inferOutputPoolSize`、`dumpflag` 等；
  - `EsPostProcess.yaml`：阈值/NMS/类别等；
  - `EsTracker.yaml`：FPS、IOU 阈值、质量阈值等（可参考 TrackerElement 支持项）。
- 可视化与输出：
  - `EsOsd.yaml`：样式/颜色/LOGO；
  - `EsVideoGrid.yaml`：网格布局（与通道数匹配）；
  - `EsVideoSink.yaml`：显示输出配置（分辨率/接口）。

---

## 4. 定制与扩展指南

- 通道在 DIE 间的分配：
  - 通过在 `die0.sh` 与 `die1.sh` 中增删 `EsAvDemux_X.yaml` 实例，并相应调整 `EsVideoGrid.yaml` 网格；
  - 负载均衡建议两侧通道数与码率总体接近，避免单侧瓶颈。
- 亲和与内存本地化：
  - 保持 `numactl` 与元素 `-die` 一致；必要时为关键算子（预处理/推理/OSD）显式设置 `-die`；
- 性能与稳态：
  - 调整 `EsMux -poolsize`、各 `EsQueue -deepth`、`perfstat_interval`；
  - 适配显示分辨率与网格，避免 OSD/拼接成为瓶颈；
- 模型与可视化：
  - 在 `EsInfer.yaml` 替换 `model-filepath` 及相关参数；
  - 结合业务调整 `EsOsd.yaml` 样式与 `EsVideoSink.yaml` 输出模式；
- 插件化扩展：
  - 按 `pipeline/src/elements/*` 模式实现自定义元素，导出工厂；
  - 通过 pl_launch 令牌机制插入到脚本（详见 `pipeline/src/pl_launch/pl_launch_readme.md`）。

---

## 5. 常见问题与排错

- 仅一侧有输出或卡顿：
  - 检查两侧 `numactl` 绑定与 `-die` 配置是否一致；
  - 核对两套 `config/` 是否对应正确的输入源与网格；
- 无检测框：
  - 核对两侧 `EsInfer.yaml` 模型路径与 `isAsync`/`inferOutputPoolSize` 等；
  - 检查 `EsPreProcess.yaml` 尺寸/归一化与模型是否一致；
- 动态库/令牌问题：
  - 确保 `espl_launch` 在 `PATH`，动态库在 `LD_LIBRARY_PATH`；元素令牌大小写与脚本一致；
- 启动脚本错误：
  - 若报 `espl_lanuch: not found`，请将脚本更正为 `espl_launch`。

---

## 6. 参考
- `pipeline/case/dualDie/start_pipeline.sh`
- `pipeline/case/dualDie/die0/die0.sh`、`pipeline/case/dualDie/die1/die1.sh`
- `pipeline/src/pl_launch/pl_launch_readme.md`
- `pipeline/src/core/core_readme.md` 与各元素 README
