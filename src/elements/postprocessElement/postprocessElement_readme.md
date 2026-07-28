# postprocessElement_readme

本文档面向使用者与二次开发者，帮助你：
- 快速理解 `postprocessElement` 的职责与核心逻辑；
- 熟悉本元素涉及的真实 SDK API 触点；
- 提供两种使用方式：编码方式构建 Pipeline 与通过 `espl_launch` 参数构建；
- 指导如何基于源码定制自己的后处理插件。

---

## 1. 功能与定位
`postprocessElement` 负责对推理输出进行通用后处理，并将结果挂载回 `CFrameMeta/CBatchMeta`：
- 分类（classification）：支持 CPU argmax，或 DSP softmax + argmax；
- 目标检测（detection）：解析模型输出，执行阈值/NMS/聚类/ROI 过滤，生成检测框；
- RTMPose（关键点/骨架）：解析关键点输出，生成姿态结果；
- 标签解析与“黄金数据”对比（可选，用于验证）。

输入/输出：
- 输入：上游推理元素产生的 `CInferOutputMeta`（包在 `CFrameMeta/CBatchMeta` 中）。
- 输出：在对应 `FrameMeta` 上填充 `objs` 或 `rtmObjs` 等结构，供 `osdElement`、`fileSinkElement`、显示/编码等下游使用。

该元素支持两类计算后端：
- CPU：位于 `cpuOp.*`，使用 `ES_SYS_Mmap/ES_SYS_Munmap` 访问推理输出内存，纯 CPU 完成 argmax / detection 解析。
- DSP：位于 `dspOp.*`，调用 ES AK DSP 算子（`ES_AK_*`）完成 softmax/argmax/检测后处理；中间结果缓冲通过 VB 池管理。

---

## 2. 源码结构与核心流程
源码目录：`pipeline/src/elements/postprocessElement/`
- 接口：`postprocessElement.h/.cpp`
- 后端实现：`cpuOp.h/.cpp`、`dspOp.h/.cpp`
- 配置与参数：`postprocessElement_config.yml`、`postprocessConfig.yml`、`postParams.h`
- 通用解析器：`../common/commyamlparser.h`

关键成员与对象：
- `PostProcessInitParams m_postprocessInitParams`：从 YAML 读取（die-id、dsp-id、network-type、optype 等）。
- `CpuOp cpuProcessData`、`std::unique_ptr<DspOp> pdspOperationObject`：后处理后端；
- `PerformanceStatic* softmaxPerformance/argmaxPerformance/detectionPerformance`：性能打点；
- `MetaPool<CObjectMeta>* ometaPool`：对象元数据内存池。

主要函数与逻辑：
- `parseConfigFile()`：
  - 读取 `postprocess-params`（die-id、dsp-id、network-type、target-inferId、optype、softmaxScale 等）；
  - 若为检测网络，解析 `detection-params`：
    - `network-name`（yolov3/4/5/7/8、ssd、frcnn）、`nms-method`、`iou-method`、`imgWH`、`input/outputTensorNum`、`classNum`、`anchorNum`、`anchorScale`、`input-scale`、`maxbboxperclass/img`、`scoreThreshold`、`iouThreshold`、`softnmssigma`、`imgOffset` 等；
  - 解析 `class-attrs-all` 与 `class-attrs-<id>` 的覆盖参数；
  - 解析 `labelfile-path` 与可选 `attach-class-ids`（半角分号分隔）。
- `Init()`：初始化性能统计、根据 `optype` 创建 `DspOp`（DSP 模式）或准备 `CpuOp`（CPU 模式）。
- `ProcessData(baseMeta, previous)`：
  - 识别 `CBaseMeta` 类型：若为 `CBatchMeta`，逐帧取 `CInferOutputMeta`；若为单帧 `CFrameMeta`，直接读取其 `infer` 输出；
  - 分类网络：调用 `cpuProcessData.classifyArgmax(...)` 或 `pdspOperationObject->classifyPostprocess(...)`，将类别与置信度写入 `objs`（或附属字段）；
  - 检测网络：调用 `detectionAttachBatchMeta(...)`，内部依据配置执行阈值、NMS/DBSCAN、ROI 过滤与 TopK，写回检测框；
  - RTMPose：`rtmposeAttachBatchMeta(...)` 解析关键点与连线，填充到 `rtmObjs`；
  - 将处理结果附着到对应 `FrameMeta`，并继续下发；
  - 遇到 `eosFlag` 透传。
- `Finish()`：释放后端资源（DSP 关闭设备与销毁 VB 池等）。

---

## 3. 真实 ES_ SDK API 触点（仅列 ES_ 前缀）
本元素直接或间接使用的真实 ES_ API（不含 `PL_ES_` 包装；涉及包装的地方一并给出真实映射）：

3.1 内存映射/释放（CPU 后端与常规 Buffers）
- `ES_SYS_Mmap(memFd, size, SYS_CACHE_MODE_...)`
- `ES_SYS_Munmap(virtAddr, size)`
说明：用于将推理输出（VB 块）映射到 CPU 地址空间读取。

3.2 DSP 算子调用（分类/检测常用）
- `ES_AK_SetDevice(devices, devNum)`
- `ES_AK_DSP_Softmax(device, ES_TENSOR_S in, ES_TENSOR_S out, float scale)`
- `ES_AK_DSP_Argmax(device, ES_TENSOR_S in, ES_TENSOR_S outValue, ES_TENSOR_S outIndex, int k, int axis)`
说明：由 `dspOp.*` 调用。输入输出的 `ES_TENSOR_S` 结构与 memFd/size 绑定。

3.3 公共视频缓冲（VB）管理（DSP 中间结果池等）
`dspOp.*` 中使用了 `PL_ES_VB_*` 包装创建/获取/释放 VB 块，对应真实 ES_ API 为：
- 创建/销毁 VB 池：`ES_VB_CreatePool`, `ES_VB_DestroyPool`
- 申请/释放 VB 块：`ES_VB_GetBlock`, `ES_VB_ReleaseBlock`
- VB 全局配置（工程 core 统一）：`ES_VB_GetConfig`, `ES_VB_SetConfig`, `ES_VB_Init`

3.4 Pipeline 生命周期（工程 core 统一）
- 系统：`ES_SYS_SetLogCfgPath`, `ES_SYS_Init`, `ES_SYS_Exit`
- 说明：元素不直接调用这些 API，但应了解其在工程层面的初始化与清理时机。

参考：`pipeline/src/core/src/pl_mem_wrap.cpp` 等处封装了 `PL_ES_*`，在文档中请仅使用真实 `ES_*` 名称。

---

## 4. 配置与参数
示例文件：`postprocessElement_config.yml`（与源码一致，已带中文注释）。核心键位一览：

- `postprocess-params`
  - `die-id`：0/1
  - `dsp-id`：DSP 设备号（如 0/1/2/3）
  - `network-type`：0=检测，1=分类（与推理模型类型对应）
  - `target-inferId`：对应上游 infer 元素的 `unique-id`
  - `optype`：0=DSP，1=CPU
  - `softmaxScale`：分类 softmax 缩放（DSP 路径）
- `detector-params`（network-type=0 生效）
  - `labelfile-path`、`cluster-mode`、`attach-class-ids`
  - `network-name`（yolo 系列/ssd/frcnn）
  - `nms-method`、`iou-method`
  - `imgWH`、`inputTensorNum`、`outputTensorNum`、`classNum`、`anchorNum`、`anchorScale`、`input-scale`
  - `maxbboxperclass`、`maxbboxperimg`、`scoreThreshold`、`iouThreshold`、`softnmssigma`、`imgOffset` 等
- `class-attrs-all` 与 `class-attrs-<id>`：全局/按类的过滤与 NMS 阈值、TopK、ROI 限制等

case 示例：`pipeline/case/**/config/EsPostProcess.yaml`（多处）。

---

## 5. 两种使用方式

5.1 编码方式构建 Pipeline
```c++
// 工厂接口（已导出）
extern "C" CElement* createEsPostProcessElement(const char* name, const char* configFile, int dieIndex);

// 创建并加入流水线
auto* post = createEsPostProcessElement("EsPost0", "/abs/path/to/EsPostProcess.yaml", 0);
pipe->AddToPipeline(post, nullptr);

// 常见链路：解码 -> 预处理 -> 推理 -> 后处理 -> OSD/编码/落盘
pipe->LinkMany(infer, post, nullptr);
pipe->LinkMany(post, osd, nullptr);
```
要点：
- postprocessElement 要求从 `FrameMeta/BatchMeta` 中获取 `CInferOutputMeta`；
- CPU 与 DSP 后端通过 `optype` 切换；DSP 需确保系统已初始化 VB 池与 DSP 设备可用；
- 分类/检测/RTMPose 的配置需与模型输出形状相匹配（tensor 数与维度）。

5.2 通过 `espl_launch` 命令参数构建
- 当前 `src/pl_launch/pl_launch.cpp` 未内置 `EsPostProcess` 分支，可参考其他元素扩展：解析 `-path/-die`，动态装载并调用 `createEsPostProcessElement`；
- 也可直接以“编码方式”集成。

命令模板（完成扩展后）：
```bash
espl_launch \
  EsAvDemux  -path EsAvDemux.yaml  -die 0 ! \
  EsVdec     -path EsVdec.yaml     -die 0 ! \
  EsInfer    -path EsInfer.yaml    -die 0 ! \
  EsPostProcess -path EsPostProcess.yaml -die 0 ! \
  EsOsd      -path EsOsd.yaml      -die 0 ! \
  EsVenc     -path EsVenc.yaml     -die 0 !
```

---

## 6. 二次开发与最佳实践
1) 接口与内存管理
- 继承 `CElement`，实现 `Init/ProcessData/perfStat/Finish/InfoQuery`；
- 读取上游 `CInferOutputMeta` 时，谨慎处理 memFd/blkSize 的映射与解映射（`ES_SYS_Mmap/Munmap`）；
- 统一在工程 core 层使用真实 `ES_VB_*` 管理公共缓冲；避免泄漏（`ES_VB_ReleaseBlock`）。

2) DSP/CPU 后端选择
- 小模型/低并发可选 CPU；高并发/低功耗优先 DSP，注意复用 VB 池，降低 `PL_ES_VB_GetBlock` 频次；
- 为关键路径加 `PerformanceStatic` 打点，定期 `perfStat()` 输出。

3) 检测/分类/姿态扩展
- 检测：根据模型输出结构调整 `anchorScale/input-scale/nms/iou/阈值` 等配置，确保解析维度与模型一致；
- 分类：支持 `argmaxK` 扩展与 Top-K 输出；
- 姿态：根据关键点数量与连线关系扩展 `rtmposeAttachBatchMeta`。

4) 与 SDK API 的文档规范
- 对外文档仅暴露 `ES_*` 名称；出现 `PL_ES_*` 的地方在注释中注明对应 `ES_VB_*`/`ES_SYS_*` 真实 API；
- 若新增与解码/编码/显示/推理相关联的后处理路径，请直接引用对应模块 `ES_` API（如 `ES_VDEC_*`、`ES_VENC_*`、`ES_NPU_*`）。

---

## 7. 参考文件
- `pipeline/src/elements/postprocessElement/postprocessElement.h/.cpp`
- `pipeline/src/elements/postprocessElement/cpuOp.*`, `dspOp.*`, `postParams.h`
- 配置：`postprocessElement_config.yml`、`postprocessConfig.yml`、`pipeline/case/**/config/EsPostProcess.yaml`
- core：`pipeline/src/core/src/pipeline.cpp`、`pipeline/src/core/src/pl_mem_wrap.cpp`（真实 `ES_*` 生命周期与 VB 封装）
- 启动器：`pipeline/src/pl_launch/pl_launch.cpp`（可参考其余元素的装载方式扩展 `EsPostProcess`）
