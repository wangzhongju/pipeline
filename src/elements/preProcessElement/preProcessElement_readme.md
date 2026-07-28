# preProcessElement_readme

本文档面向使用者与二次开发者，帮助你：
- 快速理解 `preProcessElement` 的职责与核心逻辑；
- 熟悉本元素涉及的真实 SDK API；
- 提供两种使用方式：编码方式构建 Pipeline 与通过 `espl_launch` 参数构建；
- 指导如何基于源码定制自己的预处理插件。

---

## 1. 功能与定位
`preProcessElement` 负责将上游视频帧或 ROI 区域（可来源于 detection 的 bbox）转换为下游推理所需的 NCHW/NHWC 张量格式，常见处理包含：
- 尺寸变换与裁剪（按保持比例/补边策略）；
- 颜色空间与像素格式组织（RGB/BGR/GRAY，NCHW/NHWC 排布）；
- 归一化（Min-Max 或 Z-Score）；
- 批量打包（batch 拼装，支持从多帧或多 ROI 组合一个 batch 输出）；
- IOVA/VB 缓冲管理与零拷贝对接 NPU 推理层；
- 可选的 dump 调试输出。

输入/输出：
- 输入：`CBatchMeta`，其中每个 `CFrameMeta` 持有 `CImage`（NV12 等）。若配置了级联（next-infer），也可按检测框裁剪 ROI 作为输入。
- 输出：把预处理后的张量写入 VB 块并封装到 `CPreprocessMeta`，附着回 `CBatchMeta`，供 `inferElement` 消费。

---

## 2. 源码结构与核心流程
源码目录：`pipeline/src/elements/preProcessElement/`
- 元素实现：`preProcElement.h/.cpp`
- 配置解析：`yaml_parser.h/.cpp`
- 配置样例：`preProcessElement_config.yml`、`preprocess.yaml`

关键成员与对象（摘自 `preProcElement.h`）：
- `PREPROC_PARAM_S preProcParam`：从 YAML 装载的预处理参数（输出 shape、格式、归一化、比例保持、interval、next-infer 等）。
- `VB_POOL pool; ES_S32 poolCount;`：输出批数据的 VB 池与块计数。
- `MetaPool<CPreprocessMeta>* premetaPool;`：预处理元数据内存池。
- 队列与线程：`inputBaseMetaQueue`、`outputBaseMetaQueue`、`releaseIOVAQueue`，线程 `process()`、`prepareIOVA()`、`releaseIOVA()` 以流水并行提高吞吐。
- 性能统计：`PerformanceStatic* normalCost/dumpCost/...`。

核心流程：
1) `Init()`
   - 解析 YAML：`parse_config_file(&preProcParam, m_configFile)`；
   - 初始化 VPS：`ES_VPS_Init()`；
   - 创建 `premetaPool`，准备性能统计；
   - 清理 interval 相关计数与起始状态。
2) `Start()`
   - 创建/初始化 VB 池并启动并行线程：
     - `prepareIOVA()`：从 VB 池申请输出块，准备 IOVA；
     - `process()`：核心预处理流水（读取帧/ROI -> 计算源/目的矩形 -> 调用 VPS 归一化/缩放 -> 写入输出 VB）；
     - `releaseIOVA()`：下游使用完成后释放 IOVA/VB 块。
3) `ProcessData(baseMeta, prev)`
   - 处理 `CBatchMeta`：按 `interval` 或 `next-infer` 配置选取输入图像/ROI；
   - 按 `output-order` 和 `output-format` 设置输出张量布局与 stride/offset；
   - 可选保持宽高比并补边（计算 `rectSrc/rectDst`，记录 `aspectRatio`）；
   - 若启用 `normalize`，设置 `VPS_NORMALIZATION_PARAMS_S`；
   - 调用 `ES_VPS_Normalization(frameIn, frameOut, pParams, &rectSrc, &rectDst)` 写入目标 VB；
   - 将 `CPreprocessMeta` 填入 `CBatchMeta` 并下发；
   - 处理 EOS 透传与队列收尾。
4) `Finish()`
   - 停止线程，释放 VB 池与资源；统计性能。

说明：源码中存在少量 `#if 0` 的历史或调试路径，实际生效路径以未屏蔽逻辑为准。

---

## 3. 真实 ES_ SDK API 触点（仅列 ES_ 前缀）
本元素直接或间接使用的真实 ES_ API（不含 `PL_ES_` 包装；涉及包装的地方一并给出真实映射）：

3.1 VPS（视频前处理/归一化/缩放）
- `ES_VPS_Init()` / `ES_VPS_Deinit()`：VPS 初始化/反初始化。
- `ES_VPS_Normalization(VIDEO_FRAME_S* in, VIDEO_FRAME_S* out, VPS_NORMALIZATION_PARAMS_S* params, RECT_S* src, RECT_S* dst)`：
  - 核心预处理调用，支持缩放、裁剪、颜色/排布转换与归一化；
- 可选：`ES_VPS_SetProperty(VPS_PROPERTY_E, void*)`（源码示例注释中展示，按需切换硬件引擎等）。

3.2 VB（公共缓冲池与块）
源码中分布有 `PL_ES_VB_*` 包装，真实 ES_ API 对应：
- 创建/销毁 VB 池：`ES_VB_CreatePool`, `ES_VB_DestroyPool`
- 申请/释放 VB 块：`ES_VB_GetBlock`, `ES_VB_ReleaseBlock`
- IOVA 管理：`ES_VB_AllocIOVA(VB_UID_E uid, ES_U64 memFd, ES_VOID** pIOVA)`, `ES_VB_FreeIOVA(VB_UID_E uid, ES_U64 memFd)`
- VB 全局配置（工程 core 统一）：`ES_VB_GetConfig`, `ES_VB_SetConfig`, `ES_VB_Init`

3.3 系统/日志（工程全局生命周期）
- `ES_SYS_Init`, `ES_SYS_Exit`, `ES_SYS_SetLogCfgPath`（由 pipeline/core 在更高层统一管理；本元素需要这些前置）。

注：`es_sys_memory.h/es_vb_memory.h/es_vps.h` 为以上 API 的头文件引用，具体封装可参考 `pipeline/src/core/src/pl_mem_wrap.cpp` 与 `pipeline/src/core/src/pipeline.cpp`。

---

## 4. 配置与参数
本元素的 YAML 解析实现见 `yaml_parser.cpp`，与 `preProcessElement_config.yml` 的键位保持一致。核心键位如下：

- 全局
  - `die-id`：DIE/芯片编号（0/1）
  - `target-infer-ids`：本元素输出将发送到的 infer 节点 ID 列表
  - `select-class-ids`：当使用 ROI 级联时，过滤保留的类别 ID 集合
  - `interval: [numerator, denominator]`：帧间隔控制（源码中以 changeCnt 比较实现抽帧）
  - `poolsize`：输出 VB 池容量；`out_pool_size`：预处理 meta 池大小
  - `channel`：输入图像通道索引（如 0/1）

- 输出 shape/格式
  - `output-order`：`1=NCHW`, `2=NHWC`（yaml_parser.cpp 中枚举为 `CDataFormat`，配置值按实现注释）
  - `output-format`：像素格式枚举（RGB/BGR/GRAY 等，转换为 `PIXEL_FORMAT_E`）
  - `output-shape`：根据 `output-order` 传 `[N,C,H,W]` 或 `[N,H,W,C]`
  - `data-type`：数据精度（如 INT8/FP32 等），参与像素格式与 Bpp 计算

- 纵横比/补边与归一化
  - `maintain_aspect_ratio.enable`、`padding-value`、`keep-long-side`、`keep-dst-ratio`
  - `normalize.enable`、`normalizationmode`（`VPS_NORMALIZATION_MIN_MAX`/`VPS_NORMALIZATION_Z_SCORE`）、
    `maxminreciprocal/minvalue` 或 `stdreciprocal/meanvalue`、`stepreciprocal` 等

- 级联与裁剪
  - `crop.enable`：若为 true，按上游检测框裁剪 ROI
  - `next-infer`：用于指定下游级联 infer 的参数组（如阈值、ROI 扩展、过滤大小等：`scoreThreshold/extendWidth/extendHeight/filterWidth/filterHeight`）

更多样例可参考：
- `pipeline/case/**/config/EsPreProcess.yaml`（codec, dualDie, elementTest, msi, nvr, od, odv4l2 等多处）

---

## 5. 两种使用方式

5.1 编码方式构建 Pipeline
```c++
// 工厂接口（已导出）
extern "C" CElement* createEsPreProcessElement(const char* name, const char* configFile, int dieIndex);

// 创建并加入流水线
auto* pre = createEsPreProcessElement("EsPre0", "/abs/path/to/EsPreProcess.yaml", 0);
pipe->AddToPipeline(pre, nullptr);

// 常见链路：解码 -> 预处理 -> 推理 -> 后处理 -> OSD/编码/落盘
pipe->LinkMany(vdec, pre, nullptr);
pipe->LinkMany(pre, infer, nullptr);
```
要点：
- 需保证系统与 VB/VPS 已在工程层完成初始化；
- 输出张量的 shape/格式需与下游 infer 模型输入一致；
- 若启用 ROI 级联，确保上游已写入检测结果（`CObjectMeta`），并正确配置 `select-class-ids/next-infer`；
- 大吞吐时建议合理设置 `poolsize/out_pool_size`，避免阻塞。

5.2 通过 `espl_launch` 命令参数构建
- 当前 `src/pl_launch/pl_launch.cpp` 未内置 `EsPreProcess`，可参考其他元素扩展：解析 `-path/-die`，动态装载并调用 `createEsPreProcessElement`；
- 也可直接以“编码方式”集成。

命令模板（完成扩展后）：
```bash
espl_launch \
  EsAvDemux  -path EsAvDemux.yaml  -die 0 ! \
  EsVdec     -path EsVdec.yaml     -die 0 ! \
  EsPreProcess -path EsPreProcess.yaml -die 0 ! \
  EsInfer    -path EsInfer.yaml    -die 0 ! \
  EsPostProcess -path EsPostProcess.yaml -die 0 ! \
  EsOsd      -path EsOsd.yaml      -die 0 ! \
  EsVenc     -path EsVenc.yaml     -die 0 !
```

---

## 6. 二次开发与最佳实践
1) 接口与内存管理
- 继承 `CElement`，实现 `Init/Start/ProcessData/perfStat/Finish/InfoQuery`；
- 使用 VB 池批量化输出，统一采用真实 `ES_VB_*` API 管理块生命周期（申请、映射、释放），避免泄漏；
- `ES_VPS_Normalization` 是关键耗时路径，建议保留/完善 `PerformanceStatic` 打点；

2) 纵横比与 ROI
- 统一通过 `dstrect_calculate` 或等价逻辑计算 `rectSrc/rectDst`，保持帧内一致性；
- 启用 `maintain_aspect_ratio` 时，注意目的尺寸需为偶数对齐（源码中对 x/y/width/height 做 2 对齐）；
- ROI 级联时，可结合 `extendWidth/extendHeight` 做边界扩展，避免截断目标。

3) 线程与吞吐
- `prepareIOVA/process/releaseIOVA` 三线程流水提升并发；
- 高并发场景合理设置 VB 池块大小与数量（`size_per_batch`/`poolsize`），减少 `ES_VB_GetBlock` 争用；
- 若使用 IOVA，建议初始化阶段就完成分配并复用。

4) 与 SDK API 的文档规范
- 对外文档仅暴露 `ES_*` 名称；出现 `PL_ES_*` 的地方在注释中注明对应的 `ES_VB_*` 真实 API；
- 若新增预处理算子（如色彩变换、去噪等），优先复用 `ES_VPS_*` 能力，必要时在 core 层新增封装并文档化对应 ES_ API。

---

## 7. 参考文件
- `pipeline/src/elements/preProcessElement/preProcElement.h/.cpp`
- 配置：`preProcessElement_config.yml`、`preprocess.yaml`、`pipeline/case/**/config/EsPreProcess.yaml`
- core：`pipeline/src/core/src/pipeline.cpp`、`pipeline/src/core/src/pl_mem_wrap.cpp`（真实 `ES_*` 生命周期与 VB 封装）
- 启动器：`pipeline/src/pl_launch/pl_launch.cpp`（可参考其余元素的装载方式扩展 `EsPreProcess`）
