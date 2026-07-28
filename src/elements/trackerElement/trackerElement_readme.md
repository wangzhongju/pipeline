# trackerElement_readme

本文档面向使用者与二次开发者，帮助你：
- 快速理解 `trackerElement` 的职责与核心逻辑；
- 熟悉与本元素相关的真实 ES_ SDK API 触点；
- 提供两种使用方式：编码方式构建 Pipeline 与通过 `espl_launch` 参数构建；
- 指导如何基于源码定制和扩展跟踪能力。

---

## 1. 功能与定位
`trackerElement` 负责对进入的每帧检测结果（对象框）进行时序关联与ID分配（跟踪），输出稳定的 `trackerId` 与可能的预测框信息。典型用途：
- 为检测/后处理/OSD/业务逻辑提供连续目标的唯一ID；
- 结合多分支推理与ROI裁剪，提升多目标场景的管理与统计效果；
- 统一封装对内部跟踪库（`EsTrackerMgr`）的初始化、调用与性能统计。

输入/输出：
- 输入：来自上游（通常是预处理/后处理后）的 `CBatchMeta`，其中每个 `CFrameMeta` 包含：
  - `images`：第1路索引的 `CImage` 保存 NV12 帧（用于必要的图像条件，如尺寸）；
  - `objs`：上游检测出的对象列表（每个 `CObjectMeta` 包含归一化的检测框、置信度、类别等）。
- 输出：在原有 `objs` 的基础上，填充或追加包含 `trackerId`、`trackerBboxInfo` 等跟踪信息的对象。

线程模型：
- 本元素不自建线程；在流水线的拉流/推流线程中同步执行 `ProcessData`。

下游数量：
- 对下游没有硬性数量限制；常作为检测后与OSD/编码/统计模块之间的中间处理节点。

---

## 2. 源码结构与核心流程
目录：`pipeline/src/elements/trackerElement/`
- 头/源文件：`trackerElement.h/.cpp`
- 配置：`trackerElement_config.yml`
- 工厂导出：`createEsTrackerElement(const char* name, const char* path, int dieIndex)`

核心流程：
1) `Init()`
   - 解析 YAML 配置（详见第4节）；填充 `EsTrackerSettings`；
   - 可选解析 `labelfilePath` 标签文件，用于类别ID到类别名的映射；
   - 初始化性能统计器 `trackPerformance`、`trackProcPerformance`。
2) `ProcessData(CBaseMeta* baseMeta, ...)`
   - 将上游 `CBatchMeta` 逐帧处理；对每帧：
     - 取得 `frameMeta->images[1]` 的 `VIDEO_FRAME_INFO_S`，用于获取帧宽高；
     - 根据 `frameMeta->objs`（检测目标集合）构造 `ES_TENSOR_S faceTensor`，其 shape 为 `[1, C=7, 1, faceNum, 1, 4]`，按顺序写入 [x1,y1,x2,y2,score,boxIndex,classId]；
     - 构造 `imageTensor`（当前实现占位，用于接口一致性）；
     - 按 `pad_index` 获取/初始化 `EsTrackerMgr::Instance(pad_index)` 并调用 `Process(imageTensor, faceTensor, trackBoxes)`；
     - 将 `trackBoxes` 回写/追加到 `frameMeta->objs`，形成带 `trackerId` 的对象集合；
     - 每10帧调用一次 `mEsSeqLinesMgr.GetFinished()` 进行内部清理；
   - 记录性能统计并返回。
3) `Finish()`
   - 释放性能统计器、清理缓存映射。

辅助函数：
- `parseLabelsFile(path)`：解析标签映射（id:label）；
- `WriteTrackIDPre(pl_objects, tracked_objects, width, height)`：将 tracker 输出的 `EsTrackBoxS` 同步到 `CObjectMeta` 列表，必要时追加新对象。

---

## 3. 真实 ES_ SDK API 触点（仅列 ES_ 前缀）
说明：`trackerElement` 本身不直接调用底层硬件/系统类 ES_ 函数（如 `ES_VB_*`、`ES_SYS_*`、`ES_VDEC_*`、`ES_NPU_*` 等）；其主要职责是对上游产生的元数据进行整合并调用项目内的跟踪库 `EsTrackerMgr`（位于 `impl_EsTracker.hpp`/`es_tracker_api.h` 相关实现）。

因此：
- 直接调用的“函数式” ES_ SDK API：无。
- 相关的 ES_ 类型与枚举在数据建模中出现：
  - `ES_TENSOR_S`、`ES_DATA_PRECISION_E`：用于以张量形式组织输入（图像占位、检测框）。
  - `VIDEO_FRAME_INFO_S`、`VIDEO_FRAME_S`：用于获取帧宽高（来自视频公共头 `video.h`）。

如需了解 `PL_ES_` 到真实 `ES_` 的映射，可参考 `pipeline/src/core/src/pl_mem_wrap.cpp`（与本元素无直接耦合）。若业务需要在 tracker 中加入内存池/映射或解码缓存访问，请遵循其他元素中对 `ES_VB_*`、`ES_SYS_*` 的使用范式。

---

## 4. 配置与参数
配置文件：`trackerElement_config.yml`

主要键值（与 `Init()` 解析一致）：
- 基础/阈值：
  - `initialTrackLen: int`：初始化跟踪所需最小连续帧数；
  - `fps: int`：帧率；
  - `trackerPredictMethod: int`：预测方法（枚举 `EsTrackerPredictMethod`）；
  - `normalIouThresh: float`、`kalmanIouThresh: float`、`optiflowIouThresh: float`：多策略 IOU 阈值；
  - `emitQualityThresh: int`：输出质量阈值；
  - `initTrackAreaThresh: int`：初始化时的最小目标面积；
  - `orientationDecayClipAreaThresh: int`：姿态衰减裁剪面积阈值；
  - `l1GradientThresh: float`、`checkDetScore: float`、`abandonThresh: float`：检测质量/放弃策略阈值；
  - `prevDetectUsedMax: int`：最多回看多少帧历史检测；
  - `disableOrientationOffsetRatio: float`：小目标是否关闭姿态偏移的比例阈值；
- 调试与标签：
  - `dump-flag: bool`：是否进行调试 dump；
  - `labelfilePath: string`：类别标签文件路径（`id:label`）

示例参考仓库内同名 yml 文件的注释版本。

---

## 5. 两种使用方式

### 5.1 编码方式构建 Pipeline
```c++
// 工厂接口（已导出）
extern "C" CElement* createEsTrackerElement(const char* name, const char* path, int dieIndex);

// 1) 创建元素（指定 YAML 配置路径）
auto* tracker = createEsTrackerElement("EsTracker0", "pipeline/src/elements/trackerElement/trackerElement_config.yml", 0);

// 2) 加入流水线并链接（示例：PostProcess -> Tracker -> OSD）
pipe->AddToPipeline(post,    nullptr);
pipe->AddToPipeline(tracker, nullptr);
pipe->AddToPipeline(osd,     nullptr);
pipe->LinkMany(post, tracker, osd, nullptr);

// 3) 运行
pipe->Start();
pipe->Wait();
```
要点：
- 上游需提供 `CFrameMeta::objs`（检测框集合）与 `images[1]` NV12 帧信息；
- `pad_index` 建议在多路输入时区分，tracker 会按 `pad_index` 管理实例；
- 根据需要调整 yml 中各阈值与策略。

### 5.2 通过 `espl_launch` 参数构建
- 参考 `pipeline/src/pl_launch/pl_launch.cpp` 的 `main`，为 `EsTracker` 添加解析路径与构造逻辑：
  1) 解析 `-path`（配置路径）与 `-die`（设备号/卡号，可选）；
  2) 调用工厂 `createEsTrackerElement`；
  3) 将其衔接在 PostProcess 与下游（如 OSD/Encode）之间。

命令模板（完成扩展后）：
```bash
espl_launch \
  EsTestSrc -path testSrcElement_config.yml -fps 25 -loop 300 ! \
  EsPreProcess -path preProcessElement_config.yml -die 0 ! \
  EsInfer -path inferElement_config.yml -die 0 ! \
  EsPostProcess -path postprocessElement_config.yml -die 0 ! \
  EsTracker -path trackerElement_config.yml -die 0 ! \
  EsOsd -path osdElement_config.yml -die 0 !
```
- 也可参考 `pipeline/case` 下脚本风格（若本仓库未提供，可基于上述模板自建）。

---

## 6. 二次开发与最佳实践
1) 输入协议约定
- `objs` 中检测框坐标为相对（0~1）；写回时已按帧宽高换算；
- `images[1]` 的 `VIDEO_FRAME_INFO_S` 用于取帧尺寸，不做像素级操作；

2) 算法与参数扩展
- 可在 yml 中暴露更多 `EsTrackerSettings` 字段，或增设不同类别的差异化阈值；
- 可替换/并存不同 `EsTrackerMgr` 实现，通过 `pad_index` 管理多路实例；

3) 性能与健壮性
- 使用 `PerformanceStatic` 量化端到端与内部处理耗时；
- 注意 `WriteTrackIDPre` 仅在 `pl_objects` 为空时追加对象，如需覆盖写策略可切换到完整版本（已在源码中给出注释示例 `WriteTrackID`）；
- 对 `m_postLabels` 的访问需确保标签文件加载成功与索引合法；

4) 与 ES_ SDK 的集成
- 本元素对 ES_ SDK 的直接调用为零；若需在 tracker 中引入显式的 ES_ 能力（如 VB 池管理、系统映射、硬件算子），请参考以下范式：
  - 内存池：`ES_VB_CreatePool`/`ES_VB_GetBlock`/`ES_VB_DestroyPool`、`ES_VB_MmapPool`/`ES_VB_GetBlockVirAddr`；
  - 系统映射：`ES_SYS_Mmap`/`ES_SYS_Munmap`；
  - 解码器帧获取：`ES_VDEC_GetFrame`（如需要直接对解码帧参与跟踪）；
- 请勿在文档或对外接口暴露 `PL_ES_` 名称，若工程层封装，请在文档中标注对应真实 `ES_` 名称。

---

## 7. 参考文件
- 元素：`pipeline/src/elements/trackerElement/trackerElement.h/.cpp`
- 配置：`pipeline/src/elements/trackerElement/trackerElement_config.yml`
- 跟踪库：`pipeline/src/elements/trackerElement/impl_EsTracker.hpp`、`es_tracker_api.h`（工程内或外部依赖）
- 公共类型：`pipeline/src/core/include/batch_meta.h`、`object_meta.h`、`video.h`
- 启动器：`pipeline/src/pl_launch/pl_launch.cpp`
- 案例：`pipeline/case/**`（如未提供，可参考本 README 的模板创建）
