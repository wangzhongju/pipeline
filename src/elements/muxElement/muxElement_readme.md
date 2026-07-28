# muxElement_readme

本说明面向用户与二次开发者，帮助你：
- 快速理解 `muxElement` 的职责与核心代码逻辑；
- 了解工程中与之相关的真实 SDK API 触点；
- 两种使用方式：编码方式构建 Pipeline、通过 `espl_launch` 命令参数构建；
- 基于源码定制/扩展自己的 element 插件。

---

## 1. 功能与定位
`muxElement` 是一个“多路合批（multi-input to batch）”的同步聚合器：
- 输入：来自多个上游 element 的 `CFrameMeta`（每路一条队列）。
- 核心功能：在等待窗口内（`m_waitTimeMs`）尽量从每个上游各取一帧，合成为一个 `CBatchMeta` 下发；同时正确处理每路的 `eos` 条件，最终在所有上游 `eos` 后发送带 `eosFlag` 的最后批次并退出。
- 典型用途：
  - 多路视频/IPC 的帧级对齐与合批（例如 N 路输入送入一个下游批处理模块）。
  - 在某些路数据积压时进行适度丢帧以追平节奏（见 IPC 减压逻辑）。

该 element 不做编解码/推理等重计算，仅做同步、聚合与批次封装。

---

## 2. 核心代码逻辑
源码位置：`pipeline/src/elements/muxElement/`
- 头文件：`muxElement.h`
- 源文件：`muxElement.cpp`
- 配置：`muxElement_config.yml`（当前未读取任何 YAML 键，作为占位）

关键数据结构与成员：
- `muxBlockQueue<T>`：线程安全有界队列，生产/消费条件变量同步，容量可配（构造参数）。
- `m_elementMapFrameMeta[MAX_VIDEO_GRP_NUM]`：每个上游对应一条 `muxBlockQueue<CFrameMeta*>`。
- `m_elementIndexMap`：将 `CElement*` 上游指针映射为队列索引。
- `bmetaPool`：`MetaPool<CBatchMeta>` 批次对象池，降低频繁申请释放开销。
- `notify`：`ConditionNotifier`，用于“所有上游队列均非空”的信号通知。

主要流程：
- `Init()`
  - 要求：`m_PreviousElementVec.size() >= 1` 且 `m_NextElementVec.size() == 1`；
  - 创建性能统计 `PerformanceStatic` 与 `bmetaPool`；初始化各路 `eos` 标记与队列指针。
- `Start()`
  - 初始化每路的帧计数 `m_frameMetaIndexVec`；
  - 建立 IPC 相关索引（`m_ipcIndexVec` 等）用于积压监控；
  - 为每个上游创建队列并在 `m_elementIndexMap` 中登记；
  - 启动后台线程 `m_transmitThread` 执行 `transToNext()`。
- `ProcessData(baseMeta, previousElement)`（由各上游线程调用）
  - 断言 `baseMeta` 为 `FRAME_META`；
  - 依据 `previousElement` 查找对应队列索引，将帧 `push_back` 入队；
  - 若检测到所有上游队列均非空，则 `notify.notify()` 唤醒聚合线程；
  - 打点日志并返回。
- `transToNext()`（聚合发送线程）
  - 首次等待：直到至少任一上游队列出现数据后开始主循环；
  - 主循环：
    1) `notify.wait_for(m_waitTimeMs)` 等待或超时；
    2) 申请 `CBatchMeta`：设置 `BATCH_META`、`dieIndex`、`smuxTimeout`；
    3) 统计 `eos` 路数，若全部 `eos`，则构造 `eosFlag=true` 的空批次（仅带索引）后下发并退出；
    4) 遍历上游：
       - 若该路未 `eos`，尝试 `pop()` 一帧；
       - 读到 `eos` 帧：仅置 `m_elementEosFlag[index]=true` 并对该帧 `reduceUseCount()`；
       - 读到普通帧：设置帧序号，`batchMeta->addFrameMeta(fmeta)` 加入本批次；
       - IPC 减压：若检测到该路解码积压（`gIpcDecCnt` 统计）超过阈值，可能额外 `pop()` 丢弃一帧并 `reduceUseCount()`。
    5) 若本批次 `frameSize==0`，释放 `batchMeta` 并继续循环；否则填充 `creationTime`、`batchIndex`，调用 `TransMitToNextToProcess(batchMeta)` 下发；
    6) 循环直至最终 `eos` 批次发送完毕。
- `Wait()`：join 发送线程。
- `Finish()`：释放每路队列、对象池、性能统计，并做 NUMA 资源回收。

设计要点：
- “尽量齐整”与“有限等待”的权衡：队列齐整则立即成批；否则在 `m_waitTimeMs` 超时后以“有多少聚多少”。
- 每路 `eos` 独立标记，最终所有路 `eos` 后输出一次 `eos` 批次。
- 对高积压路做适度丢帧（可选逻辑）以降低尾延时。

---

## 3. 与 SDK API 的关系（仅列 ES_ 前缀真实 SDK）
`muxElement` 本身不直接调用编解码/推理类 `ES_` API；其职责为同步聚合与批次封装。但整个工程在 core 层存在系统与缓冲区管理等真实 `ES_` API 触点，列举如下，便于读者建立整体认知：

3.1 Pipeline 生命周期（core 层，见 `pipeline/src/core/src/pipeline.cpp` 等）
- 日志与系统：
  - `ES_SYS_SetLogCfgPath`
  - `ES_SYS_Init`
  - `ES_SYS_Exit`
- 公共视频缓冲（VB）配置与初始化：
  - `ES_VB_GetConfig`, `ES_VB_SetConfig`, `ES_VB_Init`
- VB 资源管理：
  - `ES_VB_CreatePool`, `ES_VB_GetBlock`, `ES_VB_ReleaseBlock`, `ES_VB_DestroyPool`
  - 可选：`ES_VB_AllocIOVA`

说明：工程中如见 `PL_ES_*` 包装（如 `pipeline/src/core/src/pl_mem_wrap.cpp`），请在对外文档与示例中统一引用真实的 `ES_*` 名称。

3.2 与 muxElement 常见上下游模块（供链路串联参考）
- 解码：`ES_VDEC_SendStream`, `ES_VDEC_GetFrame`, `ES_VDEC_ReleaseFrame`
- 编码：`ES_VENC_*`
- 推理：`ES_NPU_LoadModelFromFile` 等
- 显示/图像处理：请参考对应元素 README，仅列 `ES_` 前缀真实 API。

---

## 4. 配置与参数
当前源码未对 `muxElement_config.yml` 进行读取与解析（无可配置项）。运行行为主要由：
- 上游数量与产出节奏；
- 构造时参数 `waitTime`（毫秒）、`muxPoolSize`、`dieIndex`；
共同决定。

未来如增加 YAML 配置解析，请在本文件补充键名、默认值、范围和生效时机。

---

## 5. 两种使用方式

5.1 通过编码方式构建 Pipeline（推荐）
示例：
```c++
// 创建 mux（等待 10ms、池大小 6、绑定 die 0）
extern "C" CElement* createEsMuxElement(int waitTime, const char* name, int muxPoolSize, int dieIndex);
CElement* mux = createEsMuxElement(10, "EsMux0", 6, 0);
pipe->AddToPipeline(mux, nullptr);

// 假设已有两个上游（例如两路解码输出）：
pipe->LinkMany(vdec0, mux, nullptr);
pipe->LinkMany(vdec1, mux, nullptr);

// mux 只有一个下游（例如一个推理批处理 element）
pipe->LinkMany(mux, infer0, nullptr);
```
要点：
- mux 要求：上游数 >= 1，且仅有 1 个下游；
- 上游需产出 `CFrameMeta`；mux 会在 `ProcessData` 中将帧入对应队列，由后台线程合批并透传。

5.2 通过 `espl_launch` 命令参数构建 Pipeline
- 当前 `src/pl_launch/pl_launch.cpp` 未内置 `EsMux` 分支（未像 `EsAvDemux`/`EsVdec` 那样 `dlopen`）。你可以：
  1) 扩展 `pl_launch.cpp`：新增 `EsMux` 的参数解析与 `dlopen`，调用 `createEsMuxElement` 创建并加入 Pipeline；
  2) 在你的业务进程中采用“编码方式”（见 5.1）。
- 若选择方案 1，可参考 `pipeline/case` 下其他脚本的风格，新增相应 case 与 YAML（若需要）。

命令模板（完成 `pl_launch` 扩展后可用）：
```bash
espl_launch \
  config_path /path/to/config \
  EsAvDemux -path EsAvDemux.yaml -die 0 ! \
  EsVdec    -path EsVdec.yaml    -die 0 ! \
  EsAvDemux -path EsAvDemux2.yaml -die 0 ! \
  EsVdec    -path EsVdec2.yaml    -die 0 ! \
  EsMux     -wait 10 -pool 6 -die 0 ! \
  EsInfer   -path EsInfer.yaml    -die 0 !
```

---

## 6. 二次开发：基于源码定制自己的 element 插件
1) 继承基类并实现接口
- 基类：`CElement`
- 建议实现：`Init/Start/Wait/ProcessData/ProcessAndTransmit/Finish/perfStat/InfoQuery`；
- 工厂导出（便于 `dlopen`）：
```c++
extern "C" CElement* createEsYourElement(const char* name, const char* path, int dieIndex);
```

2) 与 SDK 的交互规范
- 文档与示例统一仅暴露 `ES_` 前缀真实 API 名称；
- 系统/内存池等公共资源：`ES_SYS_*`、`ES_VB_*` 由 core 统一管理；
- 若你的插件涉及编解码/推理/显示，请直接引用对应模块 `ES_` API（如 `ES_VDEC_*`、`ES_VENC_*`、`ES_NPU_*`）。

3) 合批/同步最佳实践
- 多路对齐：以“所有队列均非空立即成批 + 超时回退”为基本策略；
- EOS 收敛：为每路维护独立 `eos` 标记，全部 `eos` 后发送一次 `eos` 批次并退出；
- 适度丢帧：对积压严重的路可二次 `pop()` 丢弃以降低延时；
- 资源管理：批次用完后由下游归还；对未用帧/`eos` 帧及时 `reduceUseCount()`。

4) 性能与可观测性
- 使用 `PerformanceStatic` 对关键阶段计时；
- 结合 `-die` 与线程亲和配置，利用 NUMA 绑定减少跨节点访问；
- 充分利用日志宏 `app_debug/app_info/app_warn/app_error` 定位问题。

---

## 7. 参考文件
- `pipeline/src/elements/muxElement/muxElement.h/.cpp`
- `pipeline/src/elements/muxElement/muxElement_config.yml`
- `pipeline/src/pl_launch/pl_launch.cpp`（可参考其为其他元素的装载方式扩展 `EsMux`）
- `pipeline/src/core/src/pipeline.cpp`、`pipeline/src/core/src/pl_mem_wrap.cpp`（`ES_*` 生命周期与 VB 管理）
- `pipeline/case` 下各脚本（可据此增加 mux 的 case）
