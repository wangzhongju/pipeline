# codec_readme

本 README 面向 `pipeline/case/codec` 编解码（Codec）演示用例，帮助你：
- 快速理解本 case 的整体功能与数据流拓扑；
- 参考现有实现，按需定制类似的多路解码+显示、分支编码落盘的业务；
- 正确配置与运行，体验运行效果。

关联文件位置：
- 运行脚本：
  - 标准版：`pipeline/case/codec/codec_pipeline.sh`
  - 高频版：`pipeline/case/codec/codec_pipeline_high_frequency.sh`
  - Release 版：`pipeline/case/codec/codec_pipeline_release.sh`
- 配置：`pipeline/case/codec/config/*.yaml`
- 数据流示意图：`pipeline/case/codec/image.png`

---

## 案例整体功能描述

本案例展示“多路视频解码 → 叠加可视化（OSD） → 画面拼接（VideoGrid）→ 显示输出”的实时显示链路，并通过 Tee/Demux 将一路数据分流到“多路视频编码（VENC）→ 文件落盘（FileSink）”的存储链路。脚本示例最多支持 30+ 路输入（H.264/H.265），统一汇聚后进行 OSD 与多画面拼接输出至显示，同时对若干路进行同步编码保存，验证在单设备上高密度多路编解码与显示的端到端能力。

---

## 1. 功能与数据流拓扑

以 `codec_pipeline.sh` 为基准，核心链路如下（N 路输入解码，显示支路 + 编码存储支路）：

```
[EsAvDemux xN] -> [EsVdec xN] --> EsMux("mux1") -> EsQueue -> EsTee("tee1")
                                              |                         
                                              |----> EsOsd -> EsQueue -> EsVideoGrid -> EsQueue -> EsVideoSink
                                              |
                                              |----> EsDemux -> (EsQueue -> EsVenc -> EsFileSink) x M
```
- 其中 N 为输入通道数（脚本示例 30+），M 为编码输出路数（脚本示例 15）。

各元素在本 case 中的职责：
- EsAvDemux：
  - 从文件或 IPC 拉取压缩码流（H.264/H.265），每路对应 `EsAvDemux_X.yaml`；
- EsVdec：
  - 解码为原始图像帧（如 NV12），每路对应一个解码实例，参数来自 `EsVdec.yaml`；
- EsMux（mux1）：
  - 汇聚多路解码帧，形成统一的下游输出，控制整体节拍与时序；
- EsQueue：
  - 上下游解耦的缓存队列，抗抖动、提高稳态吞吐；
- EsTee（tee1）：
  - 将来自 mux 的统一流分为显示支路与编码支路；
- 显示支路：
  - EsOsd：在帧上叠加文字/边框/Logo 等可视化信息（配置见 `EsOsd.yaml`）；
  - EsVideoGrid：按网格（如 6×5）拼接多路画面（`EsVideoGrid.yaml`），用于总览输出；
  - EsVideoSink：将画面输出至显示设备（如 HDMI），配置见 `EsVideoSink.yaml`；
- 编码存储支路：
  - EsDemux：将 Tee 的统一流拆分为多路独立子流；
  - EsVenc：对各子流进行视频编码（H.264/H.265），参数见 `EsVenc.yaml`；
  - EsFileSink：将编码码流写入文件，参数见 `EsFileSink.yaml`；

如需更直观的图示，可参考 `image.png`。

---

## 2. 运行方法

方式 A：直接运行脚本（推荐）

```bash
# 设备端 Linux shell
sh pipeline/case/codec/codec_pipeline.sh
# 或：
sh pipeline/case/codec/codec_pipeline_high_frequency.sh
# 或：
sh pipeline/case/codec/codec_pipeline_release.sh
```

脚本要点（以 `codec_pipeline.sh` 为例）：
- 环境与资源：
  - 设置 PATH、PL_LOG_LEVEL、ulimit 等；
  - 清理/创建工作目录 `/home/eswin/demo/pipeline/`；
- 启动命令：
  - `espl_launch perfstat_interval 200000 config_path $case_path/config/ ...`；
  - 依次声明 30+ 路 `EsAvDemux + EsVdec`，均并入 `EsMux -name mux1 -timeout 40 -poolsize 16`；
  - 经过 `EsQueue -> EsTee` 分两路：
    - 显示：`EsOsd -> EsQueue -> EsVideoGrid -> EsQueue -> EsVideoSink`；
    - 编码存储：`EsDemux -> (EsQueue -> EsVenc -> EsFileSink) × 15`；

方式 B：最小化通道示例（便于调试）

```bash
espl_launch perfstat_interval 200000 config_path pipeline/case/codec/config/ \
  EsAvDemux -path EsAvDemux_1.yaml -loopnum 200000000 - ! EsVdec -name d1 -path EsVdec.yaml - ! EsMux -name mux1 -timeout 40 -poolsize 8 - \
  EsAvDemux -path EsAvDemux_2.yaml -loopnum 200000000 - ! EsVdec -name d2 -path EsVdec.yaml - ! element -name mux1 - \
  ! EsQueue -name qtee -type 0 -deepth 5 - ! EsTee -name tee1 - \
  ! EsOsd -name osd1 -path EsOsd.yaml - ! EsQueue -name qgrid -type 0 -deepth 5 - ! EsVideoGrid -name grid1 -path EsVideoGrid.yaml - \
  ! EsQueue -name qvo -type 0 -deepth 5 - ! EsVideoSink -name vo1 -path EsVideoSink.yaml - \
  element -name tee1 - ! EsDemux -name demux1 - \
  ! EsQueue -name qenc1 -type 0 -deepth 5 - ! EsVenc -name venc1 -path EsVenc.yaml - ! EsFileSink -name sink1 -path EsFileSink.yaml - \
  element -name demux1 - ! EsQueue -name qenc2 -type 0 -deepth 5 - ! EsVenc -name venc2 -path EsVenc.yaml - ! EsFileSink -name sink2 -path EsFileSink.yaml -
```

---

## 3. 配置文件总览与要点

- 输入与解码：
  - `EsAvDemux_*.yaml`：每路输入源（文件/IPC、编解码类型、循环播放等）；
  - `EsVdec.yaml`：解码参数（像素格式、输出队列等）；
- 汇聚与分流：
  - `EsMux`：`-timeout` 与 `-poolsize` 影响时序与缓冲能力；
  - `EsTee`/`EsDemux`：分别负责复制与拆分流；
- 显示链路：
  - `EsOsd.yaml`：OSD 开关/样式/Logo；
  - `EsVideoGrid.yaml`：网格布局需与通道数匹配（如 6×5 合成 30 路）；
  - `EsVideoSink.yaml`：显示接口与分辨率；
- 编码与落盘：
  - `EsVenc.yaml`：编码类型、码率、GOP 等参数；
  - `EsFileSink.yaml`：输出路径/命名规则（确保磁盘写入权限与空间）。

---

## 4. 定制与扩展指南

- 通道规模：
  - 增减 `EsAvDemux_X.yaml` 与对应 `EsVdec` 实例，调整 `EsMux -poolsize`、各 `EsQueue -deepth`；
- 显示布局与可视化：
  - 根据通道数修改 `EsVideoGrid.yaml` 网格；在 `EsOsd.yaml` 调整叠加元素（文本/框/Logo）；
- 编码路数与性能：
  - 通过 `EsDemux` 后增删 `(EsQueue -> EsVenc -> EsFileSink)` 支路；合理由硬盘 IO、码率预算；
- 性能调优：
  - 适度加大 `EsMux -poolsize`、队列深度，设置 `perfstat_interval`；
  - 合理分辨率与网格，避免 OSD/拼接成为瓶颈；
- 脚本差异：
  - `codec_pipeline_high_frequency.sh` 与 `codec_pipeline_release.sh` 可在不同场景下优选，二者与主脚本结构一致，可按需对参数进行更激进/稳健的配置。

---

## 5. 常见问题与排错

- 无显示或黑屏：
  - 检查 `EsVideoSink.yaml` 分辨率/接口是否匹配显示设备；
  - 确认 `EsVideoGrid.yaml` 网格与实际通道数一致；
- 无文件输出或文件为空：
  - 检查 `EsVenc.yaml` 编码类型/码率与输入是否匹配；
  - 检查 `EsFileSink.yaml` 输出目录权限与剩余空间；
- 时序卡顿或丢帧：
  - 增大 `EsMux -poolsize` 与关键 `EsQueue -deepth`；
  - 适当降低编码路数或码率；
- 资源/库问题：
  - 确认 `espl_launch` 可执行在 PATH 中，依赖库已正确部署；

---

## 6. 参考
- `pipeline/case/codec/codec_pipeline.sh`
- `pipeline/case/codec/codec_pipeline_high_frequency.sh`
- `pipeline/case/codec/codec_pipeline_release.sh`
- `pipeline/src/pl_launch/pl_launch_readme.md`
- `pipeline/src/core/core_readme.md` 与各元素 README
