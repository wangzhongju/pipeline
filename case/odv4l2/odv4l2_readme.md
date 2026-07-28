# odv4l2_readme

本 README 面向 `pipeline/case/odv4l2` 多源目标检测（含 v4l2 直连）演示用例，帮助你：
- 快速理解本 case 的整体功能与数据流拓扑；
- 参考现有实现，定制类似的“v4l2+文件/IPC 混合输入→AI 检测→拼接显示”业务；
- 正确配置与运行，体验运行效果。

关联文件位置：
- 运行脚本：`pipeline/case/odv4l2/odv4l2_pipeline.sh`、`pipeline/case/odv4l2/odv4l2_pipeline_release.sh`
- 配置：`pipeline/case/odv4l2/config/*.yaml`
- 数据流示意图：`pipeline/case/odv4l2/image.jpg`

---

## 案例整体功能描述

本案例与 `od` 案例总体一致，但将第 1 路输入替换为 v4l2 直连采集（原始 NV12），其余多路继续采用 `EsAvDemux+EsVdec` 的压缩码流解码路径。所有路经 `EsMux` 汇聚后，进入“预处理 → 推理 → 后处理/跟踪 → OSD → 网格拼接 → 显示”的典型检测展示链路，用于验证“混合输入源 + 多路目标检测 + 实时拼接显示”的整体能力。

---

## 1. 功能与数据流拓扑

以 `odv4l2_pipeline.sh` 为准，核心链路如下：

```
EsV4l2Src(路1, NV12)  ----> \
                         EsMux("mux1") -> EsQueue -> EsPreProcess -> EsInfer -> EsQueue -> EsPostProcess -> EsTracker 
[EsAvDemux+EsVdec x24] ---->/                                             
                                                                  -> EsOsd -> EsQueue -> EsVideoGrid -> EsQueue -> EsVideoSink
```
- 路1：`EsV4l2Src` 直出原始 NV12 帧，不经解码；
- 路2-25：`EsAvDemux + EsVdec`（H.264/H.265）解码出 NV12 帧；
- `EsMux(mux1)`：聚合 25 路帧流与批次元数据；
- `EsVideoGrid`：将多路画面拼接（示例 5×5）后输出；
- `EsVideoSink`：显示至终端（如 HDMI）。

各元素在本 case 中的职责：
- EsV4l2Src：从 `/dev/videoX` 采集原始 NV12/YUV 数据，提供低时延一路输入；
- EsAvDemux：读取文件/IPC 压缩码流；
- EsVdec：硬件解码（H.264/H.265）得到 NV12；
- EsMux（mux1）：将多路帧与 batchmeta 聚合输出；
- EsQueue：解耦与缓冲，平滑吞吐；
- EsPreProcess：尺寸缩放、色彩空间转换、归一化等，适配模型输入；
- EsInfer：执行检测模型推理，支持异步与输出池大小调优；
- EsPostProcess：解析检测结果（置信度、NMS、类别等）；
- EsTracker：跟踪以稳定目标 ID/轨迹；
- EsOsd：叠加目标框/标签/轨迹与 Logo；
- EsVideoGrid：多画面拼接（如 5×5→4K 合成）；
- EsVideoSink：视频输出与显示时序控制。

---

## 2. 运行方法

方式 A：直接运行脚本（推荐）
```bash
# 设备端 Linux shell
sh pipeline/case/odv4l2/odv4l2_pipeline.sh
# 或使用发布参数：
sh pipeline/case/odv4l2/odv4l2_pipeline_release.sh
```
脚本要点（摘自 `odv4l2_pipeline.sh`）：
- 环境：设置 PATH、PL_LOG_LEVEL、ulimit，`echo 1 > /proc/eswin/vb`；
- 启动命令：
```bash
espl_launch perfstat_interval 10000000 config_path $case_path/config/ \
  EsV4l2Src -name v4l2src1 -path EsV4l2Src.yaml - ! EsMux -name mux1 -timeout 40 -poolsize 16 - \
  EsAvDemux -path EsAvDemux_2.yaml -loopnum $cloopnum - ! EsVdec -name decoder2 -path EsVdec.yaml - ! element -name mux1 - \
  ...（直至 EsAvDemux_25.yaml）... \
  ! EsQueue -name queuepreproc1 -type 0 -deepth 5 - ! EsPreProcess -name preproc1 -path EsPreProcess.yaml - ! EsInfer -name infer1 -path EsInfer.yaml - \
  ! EsQueue -name queuepost1 -type 0 -deepth 5 - ! EsPostProcess -name post1 -path EsPostProcess.yaml - ! EsTracker -name tracker1 -path EsTracker.yaml - \
  ! EsOsd -name osd1 -path EsOsd.yaml - ! EsQueue -name queuegrid1 -type 0 -deepth 5 - ! EsVideoGrid -name videogrid1 -path EsVideoGrid.yaml - \
  ! EsQueue -name queuevo1 -type 0 -deepth 5 - ! EsVideoSink -name vo1 -path EsVideoSink.yaml -
```

方式 B：最小化通道示例（便于调试）
```bash
espl_launch perfstat_interval 1000000 config_path pipeline/case/odv4l2/config/ \
  EsV4l2Src -name v4l2src1 -path EsV4l2Src.yaml - ! EsMux -name mux1 -timeout 40 -poolsize 8 - \
  EsAvDemux -path EsAvDemux_2.yaml -loopnum 200000000 - ! EsVdec -name d2 -path EsVdec.yaml - ! element -name mux1 - \
  ! EsQueue -name qpre -type 0 -deepth 5 - ! EsPreProcess -name pre -path EsPreProcess.yaml - ! EsInfer -name infer -path EsInfer.yaml - \
  ! EsQueue -name qpost -type 0 -deepth 5 - ! EsPostProcess -name post -path EsPostProcess.yaml - ! EsTracker -name trk -path EsTracker.yaml - \
  ! EsOsd -name osd -path EsOsd.yaml - ! EsQueue -name qgrid -type 0 -deepth 5 - ! EsVideoGrid -name grid -path EsVideoGrid.yaml - \
  ! EsQueue -name qvo -type 0 -deepth 5 - ! EsVideoSink -name vo -path EsVideoSink.yaml -
```

---

## 3. 配置文件总览与要点

- v4l2 输入：
  - `EsV4l2Src.yaml`：设备节点（如 `/dev/videoX`）、分辨率、像素格式（NV12）、帧率、缓冲策略等；
- 压缩码流输入与解码：
  - `EsAvDemux_*.yaml`：各路输入文件/IPC、循环播放、编码类型等；
  - `EsVdec.yaml`：解码输出像素格式/分辨率、队列深度等；
- 前处理/推理/后处理/跟踪：
  - `EsPreProcess.yaml`：尺寸/像素/归一化，需与模型预期一致；
  - `EsInfer.yaml`：
    - 必填：`model-filepath`
    - 可选：`unique-id`、`die-id`、`inferOutputPoolSize`、`dumpflag`、`isAsync`（注意参数名为 `isAsync`，不是 `inferType`）；
  - `EsPostProcess.yaml`：阈值、NMS、类别映射等；
  - `EsTracker.yaml`：FPS、IOU 阈值、质量阈值等；
- 可视化与显示：
  - `EsOsd.yaml`：叠加样式（边框/文字/颜色/Logo）；
  - `EsVideoGrid.yaml`：网格行列需与通道数匹配（示例 5×5）；
  - `EsVideoSink.yaml`：显示接口与分辨率（如 HDMI 1080p@30）。

---

## 4. 定制与扩展指南

- 输入灵活组合：
  - 可将更多路切换为 v4l2 直连或压缩码流，按需增减 `EsV4l2Src` 与 `EsAvDemux+EsVdec`；
- 通道规模与布局：
  - 增减输入数量并相应调整 `EsVideoGrid.yaml` 网格；
- 模型与性能：
  - 在 `EsInfer.yaml` 替换 `model-filepath`，并调优 `isAsync`、`inferOutputPoolSize`、`dumpflag`；
  - 视硬件资源调整 `EsMux -poolsize`、各 `EsQueue -deepth` 以平衡延迟与吞吐；
- 视觉呈现：
  - 调整 `EsOsd.yaml` 样式与 `EsVideoGrid.yaml` 布局；
- 组件扩展：
  - 参考 `pipeline/src/elements/*` 接入新元素，通过 pl_launch 令牌机制插入链路（详见 `pipeline/src/pl_launch/pl_launch_readme.md`）。

---

## 5. 常见问题与排错

- v4l2 无画面：
  - 检查设备节点/权限、像素格式/分辨率与摄像头能力是否匹配；
- 解码路异常：
  - 核对 `EsAvDemux_*.yaml` 输入路径或 IPC 参数；确认 `EsVdec.yaml` 输出参数与输入码流匹配；
- 无检测/结果异常：
  - 校验 `EsPreProcess.yaml` 与模型训练配置一致；
  - 核对 `EsInfer.yaml` 的 `model-filepath`、`isAsync`、`inferOutputPoolSize`；
  - 调整 `EsPostProcess.yaml` 阈值/NMS；
- 显示异常或花屏：
  - 核对 `EsVideoGrid.yaml` 网格与通道数一致，`EsVideoSink.yaml` 分辨率/接口匹配显示设备；
- 性能抖动/丢帧：
  - 调整 `EsMux -poolsize` 与队列深度、降低输入分辨率或帧率、减少通道数；
- 依赖/可执行：
  - 确保 `espl_launch` 在 PATH 中，相关动态库已就绪。

---

## 6. 参考
- `pipeline/case/odv4l2/odv4l2_pipeline.sh`
- `pipeline/case/odv4l2/odv4l2_pipeline_release.sh`
- `pipeline/src/pl_launch/pl_launch_readme.md`
- `pipeline/src/core/core_readme.md` 与各元素 README
