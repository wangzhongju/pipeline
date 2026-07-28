# testSinkElement_readme

本文档面向使用者与二次开发者，帮助你：
- 快速理解 `testSinkElement` 的职责与核心逻辑；
- 熟悉本元素涉及的真实 SDK API 触点；
- 提供两种使用方式：编码方式构建 Pipeline 与通过 `espl_launch` 参数构建；
- 指导如何基于源码定制自己的调试/落盘 sink 插件，以满足业务需求。

---

## 1. 功能与定位
`testSinkElement` 是一个轻量级的“终端 sink/调试器”，默认行为为：
- 打印收到的数据帧计数、元素名称与 `useCount`；
- 正确处理 EOS（End Of Stream），在 EOS 到达时进行引用计数释放；
- 提供两段可选的“数据落盘/调试 dump”代码路径（通过 `#if 0` 开关启用）：
  - 直接从 `VIDEO_FRAME_INFO_S`（NV12 等）映射内存并写出 YUV 文件；
  - 从 `CPreprocessMeta` 批张量内存映射并写出原始字节流，用于检查预处理输出。

输入/输出：
- 输入：任意 `CBaseMeta`（常见为 `CFrameMeta` 或 `CBatchMeta`）。
- 输出：无；本元素为流水线终点，不再下发。

线程模型：
- 无内部线程与队列；在 `ProcessData` 同步执行日志/可选 dump，随后 `reduceUseCount()`。

---

## 2. 源码结构与核心流程
目录：`pipeline/src/elements/testSinkElement/`
- 实现文件：`testSinkElement.h/.cpp`
- 说明/占位配置：`testSinkElement_config.yml`（当前无可配置键，便于后续扩展）

关键实现（见 `testSinkElement.cpp`）：
- `Init()`：要求仅 1 个上游；清零 `mFrameCnt`。
- `ProcessData(baseMeta, previous)`：
  - 打印日志：帧序号、元素名、`useCount`；
  - 若 `eosFlag` 为真：打印并 `reduceUseCount()`，返回；
  - 可选 dump 路径 A（注释 `#if 0`）：
    - 将 `baseMeta` 视作 `CFrameMeta`，取第一张 `CImage` 的 `VIDEO_FRAME_INFO_S`；
    - 计算 NV12 大小，`ES_SYS_Mmap(fd, size, SYS_CACHE_MODE_NOCACHE)` 获得虚拟地址；
    - `fwrite` 到 `./testsink.yuv`；`ES_SYS_Munmap`；然后 `reduceUseCount()`；
  - 可选 dump 路径 B（注释 `#if 0`）：
    - 将 `baseMeta` 视作 `CBatchMeta`，取 `mBatchedImgs[0]` 的 `CPreprocessMeta`；
    - `ES_SYS_Mmap(memFd, data_size, SYS_CACHE_MODE_NOCACHE)` 后写文件；再 `ES_SYS_Munmap`；`batchMeta->reduceUseCount()`；
  - 默认路径：仅 `reduceUseCount()` 并返回。

内存与生命周期：
- 本元素不申请 VB 资源，仅对上游传入的 FD/内存进行“只读映射”用于调试写盘；
- 必须成对调用 `ES_SYS_Mmap/ES_SYS_Munmap`；务必在写盘完成后 `reduceUseCount()`，避免泄漏。

---

## 3. 真实 ES_ SDK API 触点（仅列 ES_ 前缀）
本元素源文件包含 `es_sys.h/es_sys_memory.h/es_vb_memory.h`，实际使用（在启用相应 `#if` 路径时）如下：

3.1 系统内存映射
- `ES_SYS_Mmap(ES_U64 fd, ES_U64 size, ES_SYS_CACHE_MODE_E cacheMode)`：将视频帧/张量的 FD 对应物理内存映射到用户态；
- `ES_SYS_Munmap(ES_VOID* vaddr, ES_U64 size)`：解除映射；

3.2 VB 辅助（示例注释中的可选路径）
- `ES_VB_Fd2Handle(ES_U64 memFd)`：从 memFd 拿到 VB_BLK；
- `ES_VB_Handle2PoolId(ES_U64 memFd)`：从 memFd/块句柄拿到池 ID；
- `ES_VB_GetBlockVirAddr(VB_BLK blk, ES_VOID** vaddr)` 或 `ES_VB_GetBlockVirAddr(VB_POOL poolId, ES_U64 memFd, ES_VOID** vaddr)`：
  - 通过 VB 模块获取可直接访问的虚拟地址（与 `ES_SYS_Mmap` 路径二选一）。

3.3 系统生命周期（工程层）
- `ES_SYS_Init/ES_SYS_Exit` 等由 pipeline/core 更高层负责；本元素假定系统已初始化。

说明：
- 当前代码未出现 `PL_ES_*` 包装；若未来加入，请在文档中继续追溯到真实 `ES_*` API 名称对外呈现。

---

## 4. 配置与参数
- `testSinkElement_config.yml`：占位文件。当前源码未读取任何 YAML 键，运行无需附带配置文件。
- 可扩展项建议（若需要 YAML 控制）：
  - `dump.enable`：是否启用写盘；
  - `dump.type`：`frame`（NV12 原始帧）| `tensor`（预处理张量）；
  - `dump.path`：输出目录/文件前缀；
  - `dump.count`：最多写出多少帧/批；
  - `dump.interval`：抽帧间隔写盘；
  - `dump.sync`：同步/异步写盘策略。

---

## 5. 两种使用方式

### 5.1 编码方式构建 Pipeline
```c++
// 工厂接口（已导出）
extern "C" CElement* createEsTestSinkElement(const char* name, BASE_ELEMENT_TYPE elementType);

// 创建并加入流水线（作为终端 sink）
auto* sink = createEsTestSinkElement("EsTestSink0", VIDEO_OUTPUT);
pipe->AddToPipeline(sink, nullptr);

// 示例链路：解码 -> 预处理 -> 推理 -> 后处理 -> TestSink
pipe->LinkMany(vdec, pre, nullptr);
pipe->LinkMany(pre, infer, nullptr);
pipe->LinkMany(infer, post, nullptr);
pipe->LinkMany(post, sink, nullptr);
```
要点：
- 若你需要落盘 NV12 或张量，请在 `testSinkElement.cpp` 中打开相应 `#if 0` 代码，或将其改造为可配置；
- 注意与下游不存在链接，本元素为终点；
- 大量写盘会受磁盘 IO 限制，建议在重压测试中谨慎启用或改为异步写盘。

### 5.2 通过 `espl_launch` 参数构建
- 当前 `src/pl_launch/pl_launch.cpp` 未内置 `EsTestSink` 分支；可参照已有元素扩展：
  1) 解析 `-path/-die` 及自定义 `-dump.*` 参数；
  2) 调用 `createEsTestSinkElement`；
  3) 将其作为终端节点插入到链路末尾。

命令模板（完成扩展后）：
```bash
espl_launch \
  EsAvDemux -path EsAvDemux.yaml -die 0 ! \
  EsVdec    -path EsVdec.yaml    -die 0 ! \
  EsPreProcess -path EsPreProcess.yaml -die 0 ! \
  EsInfer   -path EsInfer.yaml   -die 0 ! \
  EsPostProcess -path EsPostProcess.yaml -die 0 ! \
  EsTestSink -die 0 !
```

---

## 6. 二次开发与最佳实践
1) Dump 能力产品化
- 将 `#if 0` 路径配置化：支持 `frame/tensor` 二选一或同时；
- 为帧/张量构建规范的文件命名（channel/timestamp/frameId/batchId）；
- 支持限速、抽帧、最大写盘数，避免拉跨实时链路；

2) 写盘性能与安全
- 建议增加异步写盘线程与环形缓冲，削峰填谷；
- 确保 `ES_SYS_Mmap/Munmap` 成对调用，错误路径也要释放；
- 对引用计数异常保留日志与断言，防止内存泄漏；

3) 与上游类型的适配
- 帧路径：解析 `CFrameMeta` 的 `VIDEO_FRAME_INFO_S`；
- 张量路径：解析 `CBatchMeta` 的 `CPreprocessMeta`（NCHW/NHWC 大小计算要与配置一致）；
- 如需同时支持更多格式（RGB/BGR/NV12/FP16/INT8），建议封装统一的 `dump_utils`；

4) SDK API 使用规范
- 对外文档仅暴露 `ES_*` 名称；若未来引入 `PL_ES_*` 包装，文档需映射回 `ES_SYS_*` 与 `ES_VB_*` 的真实调用；
- 映射选择：同一数据可通过 `ES_SYS_Mmap` 或 `ES_VB_GetBlockVirAddr` 获得虚拟地址，二者避免重复使用。

---

## 7. 参考文件
- 元素：`pipeline/src/elements/testSinkElement/testSinkElement.h/.cpp`
- 核心：`pipeline/src/core/include/base_meta.h`、`batch_meta.h`、`element.h`
- 头文件：`es_sys.h`、`es_sys_memory.h`、`es_vb_memory.h`
- 启动器：`pipeline/src/pl_launch/pl_launch.cpp`（可参考其它元素扩展 `EsTestSink`）
- 案例：`pipeline/case/**` 下的脚本可作为命令行风格参考
