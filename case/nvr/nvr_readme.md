# nvr_readme

本 README 面向 `pipeline/case/nvr` 网络视频录像（NVR）演示用例，帮助你：
- 快速理解本 case 的整体功能与数据流拓扑；
- 参考现有实现，定制类似的“多路解码→AI分析→显示/编码存储”业务；
- 正确配置与运行，体验运行效果。

关联文件位置：
- 运行脚本：`pipeline/case/nvr/nvr_pipeline.sh`、`pipeline/case/nvr/nvr_pipeline_release.sh`
- 配置：`pipeline/case/nvr/config/*.yaml`
- 数据流示意图：`pipeline/case/nvr/image.png`

---

## 案例整体功能描述

本案例展示典型 NVR 业务在单设备上的端到端实现：多路压缩码流输入经解码后，进行预处理、目标检测推理与后处理/跟踪；随后通过 OSD 可视化叠加，分流为两条支路——一条进行多画面拼接后显示（VO），另一条按通道拆分后进行视频编码并落盘存储。整体链路可支撑 25 路（示例）输入，验证多路 AI 视频分析 + 显示/存储的实时能力。

---

## 1. 功能与数据流拓扑

以 `nvr_pipeline.sh` 为准，核心链路如下：

```
[EsAvDemux x25] -> [EsVdec x25] --> EsMux("mux1") -> EsQueue -> EsPreProcess -> EsInfer -> EsQueue -> EsPostProcess -> EsTracker 
                                                                 -> EsQueue -> EsOsd -> EsTee("tee1")
                                                                                              | 
                                                                                              |-> EsQueue -> EsVideoGrid -> EsQueue -> EsVideoSink   (显示支路)
                                                                                              |
                                                                                              |-> EsDemux -> (EsQueue -> EsVenc -> EsFileSink) x K   (编码存储支路)
```
- 输入通道数示例为 25 路（`EsAvDemux_1.yaml` ~ `EsAvDemux_25.yaml`），K 为编码输出路数（脚本示例为 14 路）。

元素在本 case 中的职责：
- EsAvDemux：读取压缩码流（文件/IPC），每路一个实例；
- EsVdec：解码为原始图像帧（如 NV12），每路一个实例；
- EsMux（mux1）：汇聚多路解码帧，统一输出到下游；
- EsQueue：解耦上下游、缓冲抗抖，稳态吞吐优化；
- EsPreProcess：尺寸缩放、色彩空间转换、归一化等，准备模型输入；
- EsInfer：执行 NPU 推理（检测模型），支持异步/输出池等调优参数；
- EsPostProcess：解析推理输出（阈值、NMS、类别等）；
- EsTracker：可选跟踪，输出稳定目标 ID/轨迹；
- EsOsd：在原始帧上叠加框、标签、Logo 等可视化；
- EsTee：将统一流分为显示与编码两支；
- 显示支路：
  - EsVideoGrid：将多路画面按网格拼接（如 5×5 → 4K 画面）；
  - EsVideoSink：输出至显示设备（如 HDMI）；
- 编码存储支路：
  - EsDemux：将统一流按通道拆分；
  - EsVenc：对各子流进行视频编码（H.264/H.265）；
  - EsFileSink：将码流写入文件，实现录像存储。

更直观图示可参考 `image.png`。

---

## 2. 运行方法

方式 A：直接运行脚本（推荐）
```bash
# 设备端 Linux shell
sh pipeline/case/nvr/nvr_pipeline.sh
# 或更稳健参数：
sh pipeline/case/nvr/nvr_pipeline_release.sh
```
脚本要点：
- 环境：设置 PATH、PL_LOG_LEVEL、ulimit；创建工作目录并 `echo 1 > /proc/eswin/vb`；
- 启动命令：
  - `espl_launch perfstat_interval 10000000 config_path $case_path/config/ ...`
  - 25 路 `EsAvDemux + EsVdec` 并入 `EsMux -name mux1 -timeout 40 -poolsize 16`；
  - 后续 `EsPreProcess -> EsInfer -> EsPostProcess -> EsTracker -> EsOsd -> EsTee`；
  - 显示支路：`EsVideoGrid -> EsVideoSink`；
  - 编码支路：`EsDemux -> (EsVenc -> EsFileSink) × 14`。

方式 B：最小化通道示例（便于调试）
```bash
espl_launch perfstat_interval 1000000 config_path pipeline/case/nvr/config/ \
  EsAvDemux -path EsAvDemux_1.yaml -loopnum 200000000 - ! EsVdec -name d1 -path EsVdec.yaml - ! EsMux -name mux1 -timeout 40 -poolsize 8 - \
  EsAvDemux -path EsAvDemux_2.yaml -loopnum 200000000 - ! EsVdec -name d2 -path EsVdec.yaml - ! element -name mux1 - \
  ! EsQueue -name qpre -type 0 -deepth 5 - ! EsPreProcess -name pre -path EsPreProcess.yaml - ! EsInfer -name infer -path EsInfer.yaml - \
  ! EsQueue -name qpost -type 0 -deepth 5 - ! EsPostProcess -name post -path EsPostProcess.yaml - ! EsTracker -name trk -path EsTracker.yaml - \
  ! EsOsd -name osd -path EsOsd.yaml - ! EsTee -name tee1 - \
  ! EsQueue -name qgrid -type 0 -deepth 5 - ! EsVideoGrid -name grid -path EsVideoGrid.yaml - ! EsQueue -name qvo -type 0 -deepth 5 - ! EsVideoSink -name vo -path EsVideoSink.yaml - \
  element -name tee1 - ! EsDemux -name dmx - \
  ! EsQueue -name qenc1 -type 0 -deepth 5 - ! EsVenc -name v1 -path EsVenc.yaml - ! EsFileSink -name s1 -path EsFileSink.yaml -
```

---

## 3. 配置文件总览与要点

- 输入与解码：
  - `EsAvDemux_*.yaml` 定义每路输入（文件/IPC、循环播放、编解码类型等）；
  - `EsVdec.yaml` 定义解码输出参数（像素格式、分辨率、队列等）；
- 前处理/推理/后处理/跟踪：
  - `EsPreProcess.yaml`：尺寸/像素/归一化，需与模型预期一致；
  - `EsInfer.yaml`：
    - 必填：`model-filepath`
    - 可选：`unique-id`、`die-id`、`inferOutputPoolSize`、`dumpflag`、`isAsync`（注意参数名为 `isAsync`，不是 `inferType`）；
  - `EsPostProcess.yaml`：阈值、NMS、类别映射等；
  - `EsTracker.yaml`：FPS、IOU 阈值、质量阈值等（可参考 TrackerElement 支持项）；
- 可视化与显示：
  - `EsOsd.yaml`：文本/边框/颜色/Logo 等；
  - `EsVideoGrid.yaml`：网格布局需与通道数匹配（如 5×5 对应 25 路）；
  - `EsVideoSink.yaml`：显示接口与分辨率；
- 编码与存储：
  - `EsVenc.yaml`：编码类型、码率、GOP、分辨率等；
  - `EsFileSink.yaml`：输出目录与命名规则，确保磁盘权限与空间充足。

---

## 4. 定制与扩展指南

- 通道规模与布局：
  - 增减 `EsAvDemux_X.yaml` 与对应解码实例，同时调整 `EsVideoGrid.yaml` 网格；
- 性能与稳态：
  - 适度增大 `EsMux -poolsize`、关键 `EsQueue -deepth`，合理设置 `perfstat_interval`；
  - 平衡显示分辨率、网格规模与编码路数/码率，避免瓶颈；
- 模型替换与参数：
  - 在 `EsInfer.yaml` 切换 `model-filepath` 并调优 `isAsync`、`inferOutputPoolSize`、`dumpflag`；
  - 调整 `EsPostProcess.yaml` 阈值/NMS 以匹配新模型；
- 存储策略：
  - 在编码支路增删 `(EsQueue -> EsVenc -> EsFileSink)` 支路以控制录像路数；
- 组件扩展：
  - 可按 `pipeline/src/elements/*` 模式接入新元素，通过 pl_launch 令牌机制插入链路（详见 `pipeline/src/pl_launch/pl_launch_readme.md`）。

---

## 5. 常见问题与排错

- 无显示或黑屏：
  - 检查 `EsVideoSink.yaml` 分辨率/接口是否匹配显示设备；确认 `EsVideoGrid.yaml` 网格与通道数一致；
- 无检测/结果异常：
  - 校验 `EsPreProcess.yaml` 与模型训练配置一致；
  - 核对 `EsInfer.yaml` 的 `model-filepath`、`isAsync`、`inferOutputPoolSize`；
  - 调整 `EsPostProcess.yaml` 阈值/NMS；
- 无文件或文件为空：
  - 检查编码支路是否正确连接，`EsVenc.yaml` 码率/GOP 与输入匹配，`EsFileSink.yaml` 目录可写且有空间；
- 性能抖动/丢帧：
  - 增大 `EsMux -poolsize` 与队列深度，降低编码路数或码率，或减小显示分辨率/网格；
- 依赖/可执行：
  - 确保 `espl_launch` 在 PATH 中，相关动态库已就绪。

---

## 6. 参考
- `pipeline/case/nvr/nvr_pipeline.sh`
- `pipeline/case/nvr/nvr_pipeline_release.sh`
- `pipeline/src/pl_launch/pl_launch_readme.md`
- `pipeline/src/core/core_readme.md` 与各元素 README
