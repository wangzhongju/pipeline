# demuxElement_readme

本说明帮助你：
- 快速理解 `demuxElement` 的核心代码逻辑与其在 Pipeline 中的作用；
- 熟悉本 element 涉及的 SDK API 使用方式；
- 提供两种使用方式：1）编码方式构建 Pipeline；2）通过 `espl_launch` 命令参数构建；
- 指导如何基于源码定制自己的 element 插件。

---

## 1. 功能与定位
`demuxElement` 是一个“批次拆分/转发”类的轻量元素：
- 输入类型：`CBatchMeta` 承载的一批 `CFrameMeta`（通常由前序元素聚合产生，例如某些采集/聚合场景）。
- 核心功能：按每个 `CFrameMeta` 的 `padIndex` 将帧路由到对应的下游 element；并正确传播 `eosFlag`（流结束）。
- 典型用途：当上游把多路帧打包为一个批次下发，而你希望把它们按路由策略分发到不同下游 element（如不同的解码/推理/编码支路）。

该 element 不做编解码与容器解析，不直接依赖多媒体 SDK 的解码/显示模块（这类能力由其他 element 提供，如 `EsVdec`）。

---

## 2. 核心代码逻辑
源码位置：`pipeline/src/elements/demuxElement/`
- 头文件：`demuxElement.h`
- 源文件：`demuxElement.cpp`
- 配置：`demuxElement_config.yml`（当前无实际可配置键，作为占位说明）

关键方法速览：
- `ProcessData(CBaseMeta* baseMeta, const CElement* previousElement)`
  - 将 `baseMeta` 解释为 `CBatchMeta`。
  - 若 `eosFlag == true` 且 `getFrameMetaSize() == 0`，则为每个下游 element 构造带 `eosFlag` 的空 `CFrameMeta` 并逐一下发，完成 EOS 扩散。
  - 否则，遍历 batch 中的帧（`getFrameMeta(i, true)` 取出并持有），读取每帧的 `padIndex`：
    - 若 `padIndex` 超出下游个数，则释放该帧（`reduceUseCount()`）并跳过；
    - 否则，取下游 `m_NextElementVec[padIndex]`，设置 `eosFlag`，调用 `TransMitToNextToProcess(frameMeta, next)` 继续处理；
  - 最后对 batch 调用 `reduceUseCount()` 进行引用计数回收。
- `TransMitToNextToProcess(CBaseMeta* baseMeta, CElement* nextElement)`
  - 将 `baseMeta` 解释为 `CFrameMeta` 并调用 `nextElement->ProcessAndTransmit(frameMeta, this)`。
- `ProcessAndTransmit`/`InfoQuery`
  - 分别用于与框架对接的“处理+透传”入口、以及前后向的能力/信息查询传递。

要点：
- `demuxElement` 是线程安全的转发器，内部不持久化帧数据，仅根据 `padIndex` 做一次性路由；
- 正确的 `padIndex` 由上游元素或业务逻辑确保；
- EOS 语义：在无帧时仍确保下游能收到结束信号，便于整个 pipeline 正确收尾。

---

## 3. 与 SDK API 的关系（ES_ 分类清单）
`demuxElement` 自身不直接调用多媒体 SDK 的 `ES_` 前缀函数（不做解码/显示/内存池管理），它只是框架内的路由/转发节点。但在整个 Pipeline 生命周期中，core 层会调用一组系统与内存池相关的真实 `ES_` API。为便于你理解工程与 SDK 的接入点，列举如下（均来自 `pipeline/src/core/src/pipeline.cpp` 与相关文件）：

3.1 系统与公共资源生命周期（core 层调用）
- 日志配置/系统初始化/退出：
  - `ES_SYS_SetLogCfgPath`
  - `ES_SYS_Init`
  - `ES_SYS_Exit`
- 公共视频缓冲（VB）配置与初始化：
  - `ES_VB_GetConfig`, `ES_VB_SetConfig`, `ES_VB_Init`
- VB 资源（内存池/块）管理：
  - `ES_VB_CreatePool`, `ES_VB_GetBlock`, `ES_VB_ReleaseBlock`, `ES_VB_DestroyPool`
  - 可选：`ES_VB_AllocIOVA`

说明：工程中可能出现 `PL_ES_` 封装（位于 `pipeline/src/core/src/pl_mem_wrap.cpp`），但请在开发文档和代码示例中直接指向真实的 `ES_VB_*` 等 SDK API 名称。

3.2 与 demuxElement 搭配的常见下游能力（供串联链路时参考）
- 视频解码（例如 `EsVdec` 元素中实际调用）：
  - `ES_VDEC_SendStream`, `ES_VDEC_GetFrame`, `ES_VDEC_ReleaseFrame`
- 其他模块（编码/显示/图像处理/NPU 推理等）请参考对应元素 README 与源码，统一遵循只列 `ES_` API 的原则。

---

## 4. 配置与参数
当前源码未对 `demuxElement_config.yml` 进行读取与解析，因此“无可配置项”。该文件仅作为占位说明，方便后续扩展。若未来增加配置，请确保：
- YAML 键名与源码读取字段一一对应；
- 在 README 中补充键名、取值范围、默认值与生效时机。

---

## 5. 两种使用方式

5.1 通过编码方式构建 Pipeline
- 在你的应用中手动创建并加入 `demuxElement`：
  ```c++
  // 1) 直接 new
  CElement* demux = new DemuxElement("EsDemux0");
  pipe->AddToPipeline(demux, nullptr);

  // 2) 或通过工厂函数（demuxElement.cpp 提供）
  extern "C" CElement* createEsDemuxElement(const char* name);
  CElement* demux2 = createEsDemuxElement("EsDemux1");
  pipe->AddToPipeline(demux2, nullptr);

  // 与下游的连接
  // 假设你已有若干下游 element（如 EsVdec/EsInfer/…），可以：
  pipe->LinkMany(demux, next0, next1, /*...*/ nullptr);
  ```
- 运行时，确保上游向 `demuxElement` 提供 `CBatchMeta`，并为每个 `CFrameMeta` 设置正确的 `padIndex`（与 `LinkMany` 顺序一致）。

5.2 通过 `espl_launch` 命令参数构建 Pipeline
- 当前 `src/pl_launch/pl_launch.cpp` 未内置 `EsDemux` 分支（未像 `EsAvDemux`/`EsVdec` 那样通过 `dlopen` 加载）。
- 你有两种可选方案：
  1) 扩展 `pl_launch.cpp`：仿照 `EsAvDemux` 的实现，增加 `EsDemux` 解析分支，`dlopen` 你的 `libes_pldemux.so`（或将本 element 编成 so），并调用工厂 `createEsDemuxElement` 创建后加入 Pipeline；
  2) 不改 `espl_launch`，而是在你自己的进程中以“编码方式”（见 5.1）组装链路。
- 若选择方案 1，请同时在 `pipeline/case` 目录下新增相应的脚本，举例：
  ```bash
  espl_launch \
    config_path /path/to/config \
    EsDemux -name EsDemux0 ! \
    EsVdec  -path EsVdec.yaml -die 0 ! \
    # ... 其他元素
  ```

---

## 6. 二次开发：如何基于源码定制自己的 element 插件
1) 继承基类并实现核心接口
- 基类：`CElement`
- 建议实现/重载：`Init/Start/ProcessData/ProcessAndTransmit/TransMitToNextToProcess/Finish/InfoQuery`；
- 工厂函数：
  ```c++
  extern "C" CElement* createEsYourElement(const char* name, const char* path, int dieIndex);
  ```

2) 与 SDK 的交互规范
- 文档与示例中仅暴露真实 `ES_` SDK API 名称；
- 需要公共内存池/块时，直接使用：`ES_VB_CreatePool/ES_VB_GetBlock/ES_VB_ReleaseBlock/ES_VB_DestroyPool`（必要时 `ES_VB_AllocIOVA`）；
- 涉及解码/编码/显示/推理时，按模块引用对应 `ES_` API（如 `ES_VDEC_*`、`ES_NPU_*`），并与 Pipeline 的 `Meta` 类型（如 `CVideoPacketMeta`/`CFrameMeta`/`CBatchMeta`）对齐；

3) 路由/批处理最佳实践
- 若你的插件也需要对 batch 做切分或路由，可复用 `demuxElement` 的思路：基于 `padIndex` 做稳定的确定性转发；
- 注意引用计数与资源回收：`getFrameMeta(..., true)` 后对不再使用的 meta 调用 `reduceUseCount()`；
- EOS 的一致性与完备传播，避免下游出现“永不结束”的挂起状态。

4) 性能与可观测性
- 使用日志宏（如 `app_debug/app_info/app_warn/app_error`）对关键路径打点；
- 若涉及耗时操作，可接入 `PerformanceStatic` 统计；
- 在 `pl_launch` 场景可结合 `-die` 与 NUMA 亲和策略控制线程亲和与内存局部性。

---

## 7. 参考文件
- `pipeline/src/elements/demuxElement/demuxElement.h/.cpp`
- `pipeline/src/core/src/pipeline.cpp`（系统/VB 初始化、退出的 `ES_` API 入口）
- `pipeline/src/core/src/pl_mem_wrap.cpp`（`PL_ES_*` 封装映射到底层 `ES_VB_*` 的示例）
- `pipeline/src/pl_launch/pl_launch.cpp`（可参考其为其他 element 的装载方式来扩展对 `EsDemux` 的支持）
