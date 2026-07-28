# Pipeline 模块用户手册

## 1. 概述

Pipeline 是一个高性能、模块化的多媒体处理框架，专为视频分析、转码和流媒体处理等场景设计。它采用插件化架构，允许开发者通过组合不同的处理元素（Element）来构建复杂的媒体处理流水线。

### 主要特点
- **模块化设计**：每个功能模块（如解码、推理、编码等）都是独立的 Element，可以灵活组合
- **高性能**：支持多线程、NUMA 感知和 DIE 亲和性优化
- **可扩展**：易于添加新的处理元素或扩展现有功能
- **配置驱动**：通过 YAML 文件配置处理流程和参数
- **多场景支持**：适用于视频分析、转码、流媒体处理等多种应用场景

---
## 2. 工程目录结构

```
pipeline/
├── case/                 # 示例应用场景
│   ├── codec/            # 编解码相关案例
│   ├── dualDie/          # 多DIE架构优化案例
│   ├── elementTest/      # 元素功能测试
│   ├── msi/              # 多级推理处理案例
│   ├── nvr/              # 网络视频录像机相关案例
│   ├── od/               # 目标检测案例
│   └── odv4l2/           # 基于V4L2的目标检测案例
├── dataset/              # 数据集和资源文件
│   ├── font/             # 字体文件
│   ├── gridbg/           # 网格背景图
│   └── yolov5s_416x416_essimulator/  # 示例模型
├── models/               # 模型文件
├── src/                  # 源代码
│   ├── core/             # 核心框架代码
│   ├── elements/         # 处理元素实现
│   └── pl_launch/        # 启动脚本和工具
└── tools/                # 辅助工具
    ├── log/              # 日志工具
    └── status/           # 状态监控
```
---

## 3. 核心概念

### 3.1 Pipeline
Pipeline 是框架的核心，负责管理和调度所有的处理元素。它提供了以下功能：
- 元素的添加和链接
- 处理流程的初始化和启动
- 资源管理和线程调度
- 性能统计和监控

### 3.2 Element（处理元素）
Element 是 Pipeline 的基本构建块，每个 Element 负责特定的处理任务。Element 之间通过数据流连接，形成处理流水线。

### 3.3 数据流
数据在 Element 之间以 Meta 的形式传递，支持多种数据类型（视频帧、音频帧、推理结果等）。

---

## 4. 场景demo
 
本节对每个 Case 单独成节，给出功能说明并指向其 README：
 
### 4.1 OD（目标检测展示）
- 路径：`pipeline/case/od/od_readme.md`
- 功能：多路文件/IPC 输入→解码→检测→OSD→网格拼接→显示。
- 特点：
  - 典型"检测+展示"链路，覆盖最常用的元素组合（Demux/Vdec/Pre/Infer/Post/OSD/Grid/VO）。
  - 支持 3×3/4×4/5×5 等多画面拼接。
  - 通过 `isAsync`、`inferOutputPoolSize` 支持异步推理与多路吞吐优化。
- 适用场景：多路目标检测的看样/验通路径、功能演示与调参基线。
- 一键脚本：`case/od/od_pipeline.sh`。

### 4.2 ODV4L2（相机V4L2+解码）
- 路径：`pipeline/case/odv4l2/odv4l2_readme.md`
- 功能：第 1 路相机直连（V4L2 原始 NV12），其余路文件/IPC 解码，统一检测展示。
- 特点：
  - 单路 V4L2 原始帧与多路解码流并存，验证"相机+文件/IPC 混合"能力。
  - 打通 V4L2 源的前处理到 OSD/显示的端到端链路。
- 适用场景：线上真实相机与离线回放混合调试、混合输入融合展示。
- 一键脚本：`case/odv4l2/odv4l2_pipeline.sh`。

### 4.3 NVR（解码+检测+显示+编码存储）
- 路径：`pipeline/case/nvr/nvr_readme.md`
- 功能：多路解码→检测→分流：一支路拼接显示，另一支路按通道编码落盘（录像）。
- 特点：
  - 通过 Tee 分路形成"展示支路"和"编码存储支路"，二者互不阻塞。
  - 提供编码/码率/GOP 等参数参考，文件命名支持通道与帧序占位符。
- 适用场景：类 NVR/多路监控，边看边录；性能与存储链路联合验证。
- 一键脚本：`case/nvr/nvr_pipeline.sh`。

### 4.4 CODEC（高密度编解码验证）
- 路径：`pipeline/case/codec/codec_readme.md`
- 功能：多路解码→Tee 分支：显示支路与编码存储支路，验证编解码与显示能力。
- 特点：
  - 聚焦编解码与显示能力的压力与兼容性验证，便于单独评测编解码通路。
  - 可按通道数/分辨率/码率扩展，评估不同负载下的系统稳定性。
- 适用场景：编解码能力打点、稳定性与极限压力测试。
- 一键脚本：`case/codec/codec_pipeline.sh`。

### 4.5 MSI（多级推理）
- 路径：`pipeline/case/msi/msi_readme.md`
- 功能：展示多阶段/多模型串联的推理流水（示例）。
- 特点：
  - 演示串联的 Pre/Infer/Post 多阶段编排与数据在阶段间的传递管理。
  - 便于扩展多模型协同（如检测→分类、检测→重识别等）与性能取舍。
- 适用场景：多模型级联与任务分解的原型验证与教学示例。
- 一键脚本：`case/msi/msi_pipeline.sh`。

### 4.6 dualDie（双 DIE 并行）
- 路径：`pipeline/case/dualDie/dualDie_readme.md`
- 功能：同一设备双 DIE 并行各自多路链路；亲和配置实现计算与内存本地化。
- 特点：
  - 演示基于 `die-id` 的算力亲和与并行流水；跨 DIE 资源隔离与带宽利用。
  - 可对比单 DIE 与双 DIE 的吞吐变化，指导部署策略。
- 适用场景：多芯/多 DIE 设备的资源绑定与性能评估。
- 一键脚本：`case/dualDie/dualDie_pipeline.sh`。

### 4.7 elementTest（element自测）
- 路径：`pipeline/case/elementTest/elementTest_readme.md`
- 功能：测试源构造帧/Batch 驱动单个或部分元素，独立进行 UT 与性能验证。
- 特点：
  - 无需外部复杂输入即可验证单一 Element 的功能正确性与性能指标。
  - 适合对接新算法/自研元素时的快速单元测试与对比实验。
- 适用场景：元素级回归测试、性能评测与 Demo 验证。
- 一键脚本：`case/elementTest/elementTest_pipeline.sh`。

提示：各 Case 目录提供 `*_pipeline.sh` 作为一键运行入口，详见对应 README。

---

## 5. Element 清单一览表
 
下表列出各 Element 的职责与 README 路径：
 
| Element | 功能简介 | README |
|---|---|---|
| avDemuxElement | 读取文件/IPC 压缩码流并解复用 | `src/elements/avDemuxElement/avDemuxElement_readme.md` |
| v4l2SrcElement | V4L2 相机源，输出原始 NV12/YUV | `src/elements/v4l2SrcElement/v4l2SrcElement_readme.md` |
| demuxElement | 将聚合的批量流按通道拆分 | `src/elements/demuxElement/demuxElement_readme.md` |
| testSrcElement | 从本地 YUV 构造帧/Batch 的测试源 | `src/elements/testSrcElement/testSrcElement_readme.md` |
| vdecElement | H.264/H.265 硬件解码，产出 NV12 等帧 | `src/elements/vdecElement/vdecElement_readme.md` |
| vencElement | 视频编码（H.264/H.265） | `src/elements/vencElement/vencElement_readme.md` |
| fileSinkElement | 文件落盘（编码后码流等） | `src/elements/fileSinkElement/fileSinkElement_readme.md` |
| muxElement | 多路聚合成批处理流 | `src/elements/muxElement/muxElement_readme.md` |
| teeElement | 一进多出分流 | `src/elements/teeElement/teeElement_readme.md` |
| queueElement | 队列缓冲，解耦上下游 | `src/elements/queueElement/queueElement_readme.md` |
| preProcessElement | 缩放/色彩/归一化等预处理，适配模型输入 | `src/elements/preProcessElement/preProcessElement_readme.md` |
| inferElement | 加载模型并执行 NPU 推理，支持异步与输出池配置 | `src/elements/inferElement/inferElement_readme.md` |
| postprocessElement | 解析模型输出（阈值/NMS/类别等） | `src/elements/postprocessElement/postprocessElement_readme.md` |
| trackerElement | 多目标跟踪，输出稳定的轨迹/ID | `src/elements/trackerElement/trackerElement_readme.md` |
| osdElement | 绘制框/标签/置信度/Logo | `src/elements/osdElement/osdElement_readme.md` |
| videoGridElement | 多画面网格拼接（如 3×3、5×5） | `src/elements/videoGridElement/videoGridElement_readme.md` |
| videosinkElement | 视频输出（如 HDMI VO） | `src/elements/videosinkElement/videoSinkElement_readme.md` |
| testSinkElement | 测试接收端，打印/校验输出 | `src/elements/testSinkElement/testSinkElement_readme.md` |
 
说明：README 路径相对 `pipeline/` 根目录。

---

## 6. 编译与部署
 
1) 离线安装运行
- 将 deb 包（例如 `es-sdk-sample-pipeline-YYYY.MM_YYYY_MM_DD-riscv64.deb`）拷贝至板端；
- 安装：
  ```bash
  sudo apt install ./es-sdk-sample-pipeline-YYYY.MM_YYYY_MM_DD-riscv64.deb
  ```
- 运行示例：
  ```bash
  cd /opt/demo/pipeline/case/od/
  sudo ./od_pipeline.sh
  ```

2) 源码本地编译安装
- 依赖安装：
  ```bash
  sudo apt install cmake
  sudo apt install g++
  ```
- 构建与安装：
  ```bash
  mkdir /home/eswin/pipeline
  cd /home/eswin/pipeline
  cmake -S /opt/demo/pipeline/src
  make -j
  sudo make install
  ```

---

## 7. 如何搭建/运行 Pipeline

- 脚本方式（推荐上手）：进入某个 Case 目录，执行 `*_pipeline.sh`。脚本内部使用 `espl_launch` 串接元素，并通过 `config/*.yaml` 提供参数。
- 代码方式：参考 `pipeline/src/pl_launch/pl_launch.cpp` 中的 `main()`，以 C++ 构建图并连接各 Element，详见 `pipeline/src/pl_launch/pl_launch_readme.md`。

环境提示：
- 确保 `espl_launch` 在 PATH；必要的动态库与驱动已就绪；
- 常用运行时环境变量：若使用 VO/硬编解码，请留意权限与设备节点可用性。

---

## 8. 使用示例

以 OD Case 为例，展示如何使用 espl_launch 构建 Pipeline：

```bash
espl_launch \
  config_path ./config \
  EsAvDemux -name demux0 -path avDemux.yaml  ! \
  EsVdec    -name vdec0                      ! \
  EsQueue   -name q0 -depth 8 -type 1        ! \
  EsPreProcess -name pre0 -path preProcess.yaml ! \
  EsInfer   -name infer0 -path infer.yaml    ! \
  EsPostProcess -name post0 -path postProcess.yaml ! \
  EsOsd     -name osd0 -path osd.yaml        ! \
  EsVideoGrid -name grid0 -path videoGrid.yaml ! \
  EsVideoSink -name sink0 -path videoSink.yaml
```

说明：
- `config_path` 为全局配置根目录，元素 `-path` 均为相对路径；
- `!` 为元素间连接符；
- 每个元素通过 `-name` 指定实例名，通过 `-path` 指定配置文件；
- 可通过 `element -name {已存在元素名}` 将多个输入汇聚到同一元素。

---

## 9. 性能优化建议

1) 异步推理：在 `inferElement` 中设置 `isAsync: 1`，并合理配置 `inferOutputPoolSize`。
2) 队列缓冲：在处理耗时差异较大的元素间添加 `queueElement`，避免背压。
3) 批处理：使用 `muxElement` 聚合多路流，提高 NPU 利用率。
4) 内存复用：利用 `MetaPool` 机制减少内存分配开销。
5) NUMA 亲和：通过 `die-id` 参数将元素绑定到特定 DIE，减少跨 DIE 访问开销。

---

## 10. 故障排查

1) 日志查看：设置 `PL_LOG_LEVEL` 环境变量控制日志级别（0-ERROR, 1-WARN, 2-INFO, 3-DEBUG）。
2) 性能统计：通过 `perfstat_interval` 环境变量（单位 us）开启周期性性能统计。
3) 资源泄露：检查元素的 `Init/Finish` 是否配对调用，Meta 的引用计数是否正确释放。
4) 配置错误：确认 YAML 配置文件路径正确，参数名与元素实现一致。

---

## 11. 扩展开发

1) 自定义 Element：继承 `CElement` 基类，实现 `Init/Start/ProcessData/Finish` 等接口。
2) 配置扩展：在 YAML 中添加自定义参数，在 `parseConfigFile` 中解析。
3) 内存管理：使用 `MetaPool` 管理高频创建/销毁的对象。

参考 `pipeline/src/core/core_readme.md` 了解核心框架设计与开发指引。
