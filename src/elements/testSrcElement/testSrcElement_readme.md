# testSrcElement_readme

本文档面向使用者与二次开发者，帮助你：
- 快速理解 `testSrcElement` 的职责与核心逻辑；
- 熟悉本元素涉及的真实 SDK API 触点；
- 提供两种使用方式：编码方式构建 Pipeline 与通过 `espl_launch` 参数构建；
- 指导如何基于源码定制自己的灵活“测试/合成源”插件。

---

## 1. 功能与定位
`testSrcElement` 是一个“测试源（Test Source）”元素，用于在不依赖真实外设或上游解码的情况下，按需合成/加载多种类型的中间数据并推送给下游链路，典型用途：
- 快速联通各处理环节（预处理/推理/后处理/OSD/编码/追踪/特征比对等）；
- 快速复现场景，验证下游元素适配与吞吐；
- 作为样例展示 SDK VB/内存映射等基本用法。

输入/输出：
- 输入：无（源元素）。
- 输出：根据配置生成的 `CBaseMeta` 派生对象（如 `CBatchMeta`、`CFrameMeta`、`CPreprocessMeta`、`CInferOutputMeta`、`CAudioPacketMeta`、`CVideoGridMeta` 等）。

线程模型：
- 内部创建单一工作线程 `threadFunc()`；按 `mFps` 周期循环 `mLoopNum` 次生成数据，最后发送 EOS；
- 无内部队列；数据生成后直接调用框架推进至唯一下游（本元素约束下游数量为 1）。

下游数量约束：
- `Init()` 要求 `m_NextElementVec.size()==1`，否则返回失败。

---

## 2. 源码结构与核心流程
目录：`pipeline/src/elements/testSrcElement/`
- 头/源文件：`testSrcElement.h/.cpp`
- 配置：`testSrcElement_config.yml`、示例 `testsrc.yaml`
- 工厂导出：`createEsTestSrcElement(const char* name, const char* path, int fps, int loopnum)`

关键成员与数据结构（节选）：
- 多个 InputDataInfo 结构：`InferInputDataInfo`、`PostprocessInputDataInfo`、`TrackerInputDataInfo`、`EncodeInputDataInfo`、`CompareFaceInputDataInfo`、`VideoGridInputDataInfo`、`TTSInputDataInfo`、`AdecDataInfo` 等；
- 多个开关位：`mCreateInferData`、`mCreatePreprocData`、`mCreatePostprocessInputData`、`mCreateTrackerData`、`mCreateOsdData`、`mEncodeInputDataEnable`、`mCompareFaceEnable`、`mVideoGridEable`、`mAdecDataEnable`、`mTTSDataEnable` 等；
- VB 资源：视具体开关为不同用途创建独立 VB 池（`VB_POOL`）。

核心流程（简述）：
1) `Init()`
   - 断言下游为 1；解析 YAML 配置，逐项读取开关与参数；
   - 按需创建 VB 池（通过工程封装 `PL_ES_VB_CreatePool`，底层真实 API 为 `ES_VB_CreatePool`，见下节）；
   - 对于需加载文件数据的分支，提前统计文件大小，用于池块大小与后续拷贝。
2) `Start()`/`Wait()`
   - 启动工作线程并 join。
3) `threadFunc()`
   - 循环生成各类 Meta：
     - 预处理输入：`prepareInferInputData()` 生成 `CPreprocessMeta`（设置 dims/targetInferIds，VB 获取块并映射填充文件数据）；
     - 后处理输入：`preparePostprocessInputData()` 生成 `CInferOutputMeta`（为每个 tensor 申请 VB 块、`ES_SYS_Mmap` 写入）；
     - 编码/OSD 测试：`prepareEncodeInputData()` 读取 NV12 并构造 `CFrameMeta`/`VIDEO_FRAME_INFO_S`；
     - 追踪输入、特征比对/TTS 测试、VideoGrid、音频包等：对应 prepare 函数构造 Meta；
   - 将构造好的 `CBaseMeta` 或 `CBatchMeta` 推送到唯一下游；循环结束发送 EOS，释放资源。

辅助函数（节选，位于 `testSrcElement.cpp`）：
- `prepareInferInputData(...)`：VB 池映射方式写入推理输入；
- `preparePostprocessInputData(...)`：系统映射（`ES_SYS_Mmap`）写入后处理输入；
- `prepareEncodeInputData(...)`、`prepareTrackerData(...)`、`prepareCompareFaceData(...)`、`prepareTTSData(...)`、`prepareAudioPacketInputData(...)` 等。

---

## 3. 真实 ES_ SDK API 触点（仅列 ES_ 前缀）
本元素中直接或通过 `PL_ES_` 包装间接使用的真实 SDK API 如下（按功能分类）：

3.1 视频缓冲（VB）与内存
- 池管理：
  - `ES_VB_CreatePool(VB_POOL_CONFIG_S* cfg, VB_POOL* poolId)`：创建 VB 池（代码中经 `PL_ES_VB_CreatePool` 调用，封装实现在 `core/src/pl_mem_wrap.cpp` 第15行）。
  - `ES_VB_DestroyPool(VB_POOL poolId)`：销毁 VB 池（`PL_ES_VB_DestroyPool` 直接转调）。
- 块管理：
  - `ES_VB_GetBlock(VB_POOL poolId, ES_U64 blkSize, const ES_CHAR* mmzName, ES_U64* memFd)`：从池获取块（`PL_ES_VB_GetBlock` 封装的真实调用路径，见 `pl_mem_wrap.cpp` 第26行）。
  - `ES_VB_AllocIOVA(ES_U64 memFd, VB_UID_E uid, ES_VOID** pIOVA)`：可选，为块分配 IOVA（见 `pl_mem_wrap.cpp` 第33行，在 `bisGetIOVA` 为真时调用）。
- 池虚拟映射（便捷方式，不同于 SYS 映射）：
  - `ES_VB_MmapPool(VB_POOL poolId)` / `ES_VB_MunmapPool(VB_POOL poolId)`：将整个池映射/解映射到用户态（`prepareInferInputData` 使用）。
  - `ES_VB_GetBlockVirAddr(VB_POOL poolId, ES_U64 memFd, ES_VOID** vaddr)`：获取指定块的虚拟地址（随后对地址写入文件数据）。

3.2 系统级内存映射
- `ES_SYS_Mmap(ES_U64 fd, ES_U64 size, ES_SYS_CACHE_MODE_E cacheMode)`：将 `memFd` 对应物理内存映射到用户态；
- `ES_SYS_Munmap(ES_VOID* vaddr, ES_U64 size)`：解除映射；
  - 用途：在 `preparePostprocessInputData()` 中对每个输出张量进行写入后再解映射。

3.3 系统生命周期（工程层）
- `ES_SYS_Init/ES_SYS_Exit/ES_SYS_SetLogCfgPath` 等由更高层（pipeline 初始化）负责，本元素假定系统已初始化。

说明：
- 源码中出现的 `PL_ES_VB_*` 属于工程侧的“预取块/回收块队列”封装，真实落在上述 `ES_VB_*` API；为便于 SDK 学习，本节仅列真实 `ES_` 名称。

---

## 4. 配置与参数
配置文件示例：`testSrcElement_config.yml`（与 `Init()` 解析一致）。主要键：
- 基础开关：
  - `create_infer_data`：是否生成用于 Infer 的模拟数据；
  - `create_osd_data`：是否生成 OSD/编码测试数据（读取 NV12 文件并封装为帧）；
  - `create_preproc_data`：是否生成预处理相关数据（部分联调使用）；
  - `create_muxTest_data`：是否生成 Mux 测试数据；
- 后处理输入构造 `createPostprocessInput`：
  - `enable`、`batchSize`、`outputInferID`、`inputData`（字符串或列表）、`dims`（列表或列表的列表，项为 `[n,h,w,c]`）、`datatype`；
- 推理输入构造 `createInferInput`：
  - `enable`、`dims: [n,h,w,c]`、`batchSize`、`targetInferID`、`inputData`（文件路径）；
- 追踪输入 `createTrackerInput`：`enable`、`batch_size`、`width`、`height`、`inputFileData`；
- 人脸选择 `faceSelectData`：`enable`、`filepath`、`width`、`height`、`datatype`；
- 编码输入 `encodeInput`：`enable`、`filepath`、`width`、`height`、`datatype`；
- 特征保存 `saveDataInput`：`enable`、`feature_shape`、`source`；
- 人脸比对 `compareFaceInput`：`enable`、`batch_size`、`width`、`height`、`inputFileData`、`featureShape`、`inferBatchSize`；
- VideoGrid `videogridInput`：`enable`、`filepath`、`width`、`height`、`datatype`；
- 音频 `adecDataInput`：`enable`、`source`；
- TTS `TTSInput`：`enable`、`batch_size`、`width`、`height`、`inputFileData`、`featureShape`、`inferBatchSize`。

注意：不同开关将触发创建独立 VB 池，块大小根据输入尺寸/文件尺寸计算；内存写入通过 `ES_VB_*` 虚拟映射或 `ES_SYS_Mmap` 完成。

---

## 5. 两种使用方式

### 5.1 编码方式构建 Pipeline
```c++
// 工厂接口（已导出）
extern "C" CElement* createEsTestSrcElement(const char* name, const char* path, int fps, int loopnum);

// 1) 创建元素（指定 YAML 配置路径、帧率和循环次数）
auto* src  = createEsTestSrcElement("EsTestSrc0", "testSrcElement_config.yml", 25, 300);
auto* next = /* 下游元素，例如预处理、后处理、OSD、编码等 */;

// 2) 加入流水线并链接（testSrc 要求恰好 1 个下游）
pipe->AddToPipeline(src,  nullptr);
pipe->AddToPipeline(next, nullptr);
pipe->LinkMany(src, next, nullptr);

// 3) 运行
pipe->Start();
pipe->Wait();
```
要点：
- 将不同场景需要的键写入 `testSrcElement_config.yml`，`Init()` 会按键创建对应 VB 池并准备数据；
- 多开关可并行启用，线程将按帧循环产生对应 Meta 并传递给唯一下游；
- 循环结束会发送 EOS（`baseMeta->eosFlag=true`），下游需正确释放引用计数。

### 5.2 通过 `espl_launch` 参数构建
- 可参考 `pipeline/src/pl_launch/pl_launch.cpp` 的 `main` 实现添加 `EsTestSrc` 解析与构造：
  1) 解析 `-path`（配置路径）、`-fps`、`-loop` 等参数；
  2) 调用工厂 `createEsTestSrcElement`；
  3) 作为源节点放在链路起点，并确保仅链接 1 个下游。

命令模板（完成扩展后）：
```bash
espl_launch \
  EsTestSrc -path pipeline/src/elements/testSrcElement/testSrcElement_config.yml -fps 25 -loop 300 ! \
  EsPreProcess -path EsPreProcess.yaml -die 0 ! \
  EsInfer -path EsInfer.yaml -die 0 ! \
  EsPostProcess -path EsPostProcess.yaml -die 0 ! \
  EsOsd  -path EsOsd.yaml  -die 0 !
```
- 也可参考 `pipeline/case` 目录下脚本风格，按需组合不同下游链路。

---

## 6. 二次开发与最佳实践
1) 数据类型扩展
- 增加更多 `prepare*` 构造函数，统一通过 YAML 开关控制；
- 对帧格式（NV12/RGB/BGR/FP16/INT8）建立规范的尺寸与 stride 计算；

2) 内存与性能
- 对大批量数据建议使用 `ES_VB_MmapPool/ES_VB_GetBlockVirAddr` 批量映射写入；
- 对零散数据采用 `ES_SYS_Mmap/Munmap` 即取即用；
- 充分考虑块大小与对齐，避免越界写；

3) 资源安全
- 池与块的生命周期必须成对：CreatePool/GetBlock/写入/回传或最终释放/DestroyPool；
- 映射必须成对：Mmap/Munmap；错误路径要兜底释放；
- 循环结束发送 EOS，便于下游正确收尾；

4) 与下游适配
- 预处理输入：设置 `CPreprocessMeta.dims/targetInferIds/aspectRatio*` 与 `originFrameMetas`；
- 后处理输入：为每个输出张量构造 `CInferOutputMeta.onputDataInfo`、`memFd` 与数据类型；
- 编码/OSD：构造 `VIDEO_FRAME_INFO_S`（stride/offset/pixfmt/width/height）并附着到 `CFrameMeta`；

5) SDK API 暴露规范
- 对外文档仅暴露 `ES_*` 名称；`PL_ES_*` 的实现映射见 `core/src/pl_mem_wrap.cpp`，最终都落在 `ES_VB_*`/`ES_SYS_*`；

---

## 7. 参考文件
- 元素：`pipeline/src/elements/testSrcElement/testSrcElement.h/.cpp`
- 配置：`pipeline/src/elements/testSrcElement/testSrcElement_config.yml`、`testsrc.yaml`
- 核心封装：`pipeline/src/core/src/pl_mem_wrap.cpp`、`pipeline/src/core/include/pl_mem_wrap.h`
- 公共类型：`pipeline/src/core/include/batch_meta.h`、`element.h`、`object_meta.h`、`video.h` 等
- 启动器：`pipeline/src/pl_launch/pl_launch.cpp`
- 案例：`pipeline/case/**`
