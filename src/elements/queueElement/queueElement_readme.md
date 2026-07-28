# queueElement_readme

本文档面向使用者与二次开发者，帮助你：
- 快速理解 `queueElement` 的职责与核心逻辑；
- 熟悉本元素涉及的真实 SDK API；
- 提供两种使用方式：编码方式构建 Pipeline 与通过 `espl_launch` 参数构建；
- 指导如何基于源码定制自己的队列类元素以适配业务流控需求。

---

## 1. 功能与定位
`queueElement` 是一个通用的异步缓冲转发元素，主要用于在上游与下游之间提供：
- 异步解耦：上游写入队列、下游独立线程消费；
- 流量整形：通过队列容量/策略吸收抖动；
- 丢帧策略：在满队列场景可选择丢弃旧数据，降低时延；
- 可观测性：打印队列长度与处理耗时，便于压测与定位瓶颈。

输入/输出：
- 输入：任意继承 `CBaseMeta` 的元数据（例如 `CBatchMeta`/`CFrameMeta` 等）。
- 输出：按 FIFO 顺序透传至下一个元素，EOS 会被透传并触发线程退出。

队列模式（构造参数 `type`）：
- `1`：丢弃模式（满队列时丢弃最旧的一帧，以控制时延）；
- `0`：阻塞模式（生产者在队列满时阻塞等待，可保序不丢帧）；
- `-1`：无限长度模式（内部上限 65535，仅用于特殊测试，不建议生产使用）。

---

## 2. 源码结构与核心流程
目录：`pipeline/src/elements/queueElement/`
- 实现文件：`queueElement.h/.cpp`
- 说明/占位配置：`queueElement_config.yml`（当前无可配置键，便于后续扩展）

关键成员（见 `queueElement.h`）：
- `BaseMetaQueue m_queueData`：底层阻塞队列（定义于 `pipeline/src/core/include/queue.h`）；
- `std::thread m_queueThread`：独立消费线程；
- `int queueType`：队列策略；`int64_t m_theRealDepth`：实时队列深度；
- 继承自 `CElement` 的常规生命周期接口与链路管理。

线程与流程（见 `queueElement.cpp`）：
1) `Init()`：校验链路（本元素设计为 1 入 1 出）；
2) `Start()`：启动消费线程 `threadFunc()`；
3) `ProcessData()`：生产线程（来自上游）将 `CBaseMeta*` 入队；
   - 丢弃模式下，若满队列先弹出最旧元素并 `reduceUseCount()`；
4) `threadFunc()`：消费者线程循环 `pop()` 取出元素，记录耗时，并调用 `TransMitToNextToProcess(baseMeta)` 推送下游；
   - 遇到 `eosFlag` 时打印并退出循环；
5) `Wait()`：等待消费线程退出；`Finish()`：释放 NUMA 绑定的对象内存（`freeNumaNode`）。

NUMA/CPU 亲和性：
- 启动线程时调用 `set_thread_affinity(m_dieIndex)` 与 `getCpuNumaID(__func__)`，便于跨 DIE/NUMA 优化。
- 对象构造/销毁使用 `bindNumaNode`/`freeNumaNode`（见 `core/include/element.h`）。

---

## 3. 真实 ES_ SDK API 触点（仅列 ES_ 前缀）
`queueElement` 自身不直接调用任何 `ES_` 前缀的 SDK API。其逻辑完全基于工程通用的：
- Core 工具：`BaseMetaQueue`（阻塞队列）、`CElement` 生命周期/链路、NUMA/亲和性辅助；
- C++ 标准库：`std::thread`/`std::mutex`/`std::condition_variable` 等。

说明：
- 若你在本元素中看见 `PL_ES_*` 符号，请继续沿着 `pipeline/src/core/src` 的实现向下追踪到对应的真实 `ES_*` API 再对外文档化。然而当前版本 `queueElement` 未使用任何 `PL_ES_*` 或 `ES_*` API。

相关头文件：
- `pipeline/src/core/include/queue.h`、`pipeline/src/core/include/element.h`（NUMA/亲和性封装，内部不直接调用 ES_*）

---

## 4. 配置与参数
- `queueElement_config.yml`：占位文件。当前源码未读取任何 YAML 键，运行无需附带配置文件。
- 队列容量与策略通过构造参数注入：`QueueElement(int depth, int type, const char* name, ...)`。

建议：如需在 YAML 中配置 `depth/type` 等，请在元素 `Init()` 增加配置解析并更新此文件。

---

## 5. 两种使用方式

### 5.1 编码方式构建 Pipeline
参考 `pipeline/src/core/src/pipeline.cpp` 链接方法，直接通过工厂创建并加入流水线：
```c++
// 工厂接口（已导出）
extern "C" CElement* createEsQueueElement(int depth, int type, const char* name, int dieIndex);

// 典型用法：在两个耗时/速率不一致的元素间插入队列
auto* q = createEsQueueElement(/*depth*/ 100, /*type*/ 0, /*name*/ "EsQ0", /*die*/ 0);
pipe->AddToPipeline(q, nullptr);

// 例如：解码 -> Queue -> 预处理 -> 推理
pipe->LinkMany(vdec, q, nullptr);
pipe->LinkMany(q, pre, nullptr);

// 注意：
// - type=1 丢旧保持低时延；type=0 阻塞保序；type=-1 不限长（内部上限 65535，不建议生产使用）。
// - 若跨 DIE/NUMA，建议将 queue 与下游放在同一 die，提高 cache locality。
```

### 5.2 通过 `espl_launch` 参数构建
- 当前 `src/pl_launch/pl_launch.cpp` 未内置 `EsQueueElement` 分支。你可以参照已有元素在 `main` 中：
  1) 解析 `-path/-die`/自定义 `-depth/-type` 参数；
  2) 调用 `createEsQueueElement`；
  3) 将其插入到 `LinkMany` 链路中。

完成扩展后，可用类似命令：
```bash
espl_launch \
  EsAvDemux -path EsAvDemux.yaml -die 0 ! \
  EsVdec    -path EsVdec.yaml    -die 0 ! \
  EsQueue   -depth 100 -type 0   -die 0 ! \
  EsPreProcess -path EsPreProcess.yaml -die 0 ! \
  EsInfer   -path EsInfer.yaml   -die 0 ! \
  EsPostProcess -path EsPostProcess.yaml -die 0 ! \
  EsOsd     -path EsOsd.yaml     -die 0 ! \
  EsVenc    -path EsVenc.yaml    -die 0 !
```

---

## 6. 二次开发与最佳实践
1) 流控策略扩展
- 现有策略：阻塞/丢旧/不限长。可新增：丢新（保留最新数据）、按关键帧优先、基于时间戳水位等；
- 在 `ProcessData()` 里实现产出侧策略，在 `threadFunc()` 消费侧实现调度策略。

2) 观测与诊断
- 保留 `app_debug/app_info` 日志，必要时增加时延直方图、队列峰值深度统计；
- 将 `m_theRealDepth` 暴露到 `InfoQuery()`，方便外部监控。

3) EOS 与内存安全
- EOS 必须透传并驱动线程退出；
- 丢弃模式下务必调用 `reduceUseCount()`，避免上游引用计数泄漏；
- 如引入自定义缓冲，确保与下游生命周期对齐。

4) NUMA/亲和性
- 构造与线程启动使用 `bindNumaNode/set_thread_affinity`；
- 使 queue 与其“重计算”下游共置，减少跨 NUMA 访问；

5) YAML 配置化
- 如果需要以 YAML 控制 `depth/type` 等：
  - 在 `yaml_parser.cpp` 中定义键值解析；
  - 在 `Init()` 读取并覆盖构造参数；
  - 更新 `queueElement_config.yml` 与本文档参数表。

---

## 7. 参考文件
- 元素：`pipeline/src/elements/queueElement/queueElement.h/.cpp`
- 队列实现：`pipeline/src/core/include/queue.h`
- 基类/NUMA：`pipeline/src/core/include/element.h`
- 启动器：`pipeline/src/pl_launch/pl_launch.cpp`（可参考其它元素扩展 `EsQueue` 装载）
- 案例：`pipeline/case/**` 下的脚本可作为命令行风格参考
