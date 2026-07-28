# core_readme

本文档参考 `inferElement_readme.md` 的组织风格，聚焦 `pipeline/src/core` 的框架与基石：数据结构关系、核心执行流程、线程/内存/性能模型，并给出如何基于这些基石编写自定义元素（element）的实操指引。

目录结构（节选）：
- `include/`
  - `pipeline.h`：管线类 `CPipeLine`，提供 Add/Link/Init/Start/Wait/Finish 等生命周期管理
  - `element.h`：元素抽象基类 `CElement` 及通用工具、NUMA 绑定、统一调用入口
  - `base_meta.h` / `batch_meta.h` / `video.h` / `infer.h` / `object_meta.h`：数据元（Meta）体系
  - `queue.h` / `pool.h` / `perf_tools.h` / `sw_performance.h`：队列、内存池、性能统计
  - `log.h` / `defines.h` / `error.h` / `utilities.h`：日志、公共定义
- `src/`
  - `pipeline.cpp`：核心调度逻辑与系统初始化（ES_SYS/ES_VB）
  - `perf_tools.cpp` / `sw_performance.cpp` / `log.cpp`：统计与日志实现

---

## 1. 数据结构关系图与职责

核心对象与关系：
- CPipeLine（管线）
  - 持有按顺序注册的 `CElement*` 列表 `mElements`
  - 负责生命周期：`AddToPipeline` → `LinkMany` → `Init` → `Start` → `WaitForFinish` → `Finish`
  - 负责全局系统资源初始化与回收：VB（视频缓冲池）、SYS（系统层）
  - 周期触发 `element->perfStat()` 收集性能指标
- CElement（元素基类）
  - 统一的接口：`Init/Start/ProcessData/ProcessAndTransmit/Wait/Finish/perfStat`
  - 维护拓扑：`m_PreviousElementVec`、`m_NextElementVec`
  - 统一的转发：`TransMitToNextToProcess(baseMeta)`
  - 同步启动：`mStartFlag` 信号量 + `mRunningFlag` 保证 Start 完成后再处理数据
  - NUMA/CPU：构造阶段记录 CPU/NUMA，`get_numa_node()`、`set_thread_affinity()` 可结合 die 绑定
- Meta 体系（跨元素传递的数据单元）
  - `CBaseMeta`：基础元数据，含 `mMetaType`、`mUseCount`（原子引用计数）、`eosFlag`、`dieIndex`
  - `CFrameMeta`：单帧视频/图像相关元数据（见 `video.h`）
  - `CBatchMeta`：批量帧/复合数据容器，含 `vector<CFrameMeta*>`、`videoGrid` 等，并负责释放所含对象
  - 其他：推理输出、目标框等见 `infer.h`、`object_meta.h`
- 队列与内存管理
  - `BlockQueue<T>`：通用阻塞队列（可设容量、条件变量同步）
  - `MetaPool<T>`：NUMA 感知的对象池，预分配大块内存并构造对象，`allocate()/deallocate()` 高效复用
  - `BlockQueueManager`：管理 `plVBstr` 的队列与 memFd 映射（用于 VB 缓冲关联）
- 性能统计
  - `PerformanceStatic`：段统计器，元素/释放过程可打点 `performanceStaticStart/End/Report`

---

## 2. CPipeLine 的核心执行流程（pipeline.cpp）

1) Add 与 Link
- `AddToPipeline(ele1, ... , nullptr)`：将元素加入 `mElements`，顺序即默认数据流方向
- `LinkMany(e1, e2, e3, ..., nullptr)`：建立有向链路：`e1->m_NextElementVec.push_back(e2)` 且 `e2->m_PreviousElementVec.push_back(e1)`，依次串联

2) Init（系统与元素初始化）
- 日志初始化：`LOGPL_Init("pl_log.conf")`；可结合环境变量设置日志级别
- VB 视频缓冲：
  - 使用命名信号量保证多进程下 VB 配置只初始化一次
  - 首进程：`ES_VB_SetConfig(&vbConfig)` → `ES_VB_Init()`；后续进程：`ES_VB_GetConfig()` 读取已设配置
- 系统层：`ES_SYS_Init()`
- 元素 Init：遍历 `mElements` → `element->get_numa_node(element)` → `element->Init()`；为每个元素 `sem_init(mStartFlag,0,0)`

3) Start（线程与统计）
- 遍历元素调用 `element->Start()`，随后 `sem_post(&element->mStartFlag)` 允许其处理数据
- `isAllElementStart = true`；启动 KPI 统计线程 `PerfStatisticsTimer()`，定期 `element->perfStat()`

4) 运行态的数据流
- 数据由上游元素创建 `CBaseMeta*`（如 `CFrameMeta`/`CBatchMeta`），调用其 `ProcessAndTransmit()`
- 默认实现：
  - 若未运行则 `sem_wait(&mStartFlag)`（确保 Start 完毕）
  - `ProcessData(baseMeta, this)` 执行业务
  - `TransMitToNextToProcess(baseMeta)` 将同一 `baseMeta` 转交所有下游
- 下游元素收到同一 `baseMeta`，需基于 `mUseCount` 做正确的共享/释放（见第 3 节）

5) Wait & Finish
- `WaitForFinish()`：逐个 `element->Wait()`（存在线程的元素实现该接口）→ 停止 KPI → join 统计线程
- `Finish()`：逐个 `element->Finish()`，销毁各自资源；最后 `ES_SYS_Exit()`
- 通过 `notifyExit()` 可设置 `isVoEosFlag=true` 通知退出

---

## 3. Meta 生命周期与内存/线程模型

1) 引用计数
- `CBaseMeta::mUseCount` 为原子计数，默认 1
- 当一个 `baseMeta` 被多个下游共享：下游如需延后释放，应在自身持有时 `addUseCount()`，处理完调用 `reduceUseCount()`
- 当 `mUseCount` 递减至 0 时，`release()` 被调用：
  - `CBatchMeta::release()` 负责释放 `mBatchedImgs`、`mImgMetas`（内部逐一 `reduceUseCount()`）、推理输出、`videoGrid` 等，并支持归还到 `MetaPool`
  - 其他 `Meta` 类型各自实现清理逻辑

2) 对象池（MetaPool）
- 在高频创建/销毁的数据结构上建议使用 `MetaPool<T>`：
  - 构造：单次大块分配 + 带 padding 的对象布局，NUMA 亲和
  - `allocate()`/`deallocate()`：条件变量阻塞式出入池，降低内存碎片与锁开销

3) 线程与同步
- 元素内部若有工作线程（如队列/源类元素），在 `Start()` 创建并在 `Wait()` 中 join
- 基类 `CElement::ProcessAndTransmit()` 已处理启动栅栏（`mStartFlag`），避免 `Init` 未完就处理数据
- 结合 `set_thread_affinity(die)` 可将线程绑核以减少跨 NUMA 访问

---

## 4. 开发自定义元素（Element）指引

一个最小可用的元素需：继承 `CElement`，实现 `ProcessData()`；按需覆盖 `Init/Start/Wait/Finish/perfStat`；正确处理 `Meta` 的引用计数；完成插件工厂导出以供 `pl_launch`/`espl_launch` 装载。

关键要点：
- 拓扑与调用
  - 上游调用你的 `ProcessAndTransmit()`（默认）→ 你的 `ProcessData()` 执行业务 → 调用 `TransMitToNextToProcess(baseMeta)` 下发
  - 若你要异步（如 queue/sink）：可覆盖 `ProcessAndTransmit()`，将 `baseMeta` 压入内部 `BlockQueue`，由工作线程消费并手动调用 `TransMitToNextToProcess()`
- Meta 使用
  - 若仅透传同一 `baseMeta`，无需修改其 `mUseCount`
  - 若生成新 `Meta` 与旧数据并存，确保对持有的 `Meta` 调整引用计数并在合适时机 `reduceUseCount()`
- 初始化/资源
  - 在 `Init()` 中解析 YAML、创建池、打开设备；在 `Finish()` 中逆序释放
  - 性能统计可使用 `PerformanceStatic`，在关键路径 `Start/End` 打点并在 `perfStat()` 中 `Report`
- 工厂导出（供 launcher 动态加载）
  ```cpp
  extern "C" CElement* createEsYourElement(const char* name, const char* configFile, int dieIndex) {
      return new (bindNumaNode(dieIndex, sizeof(YourElement))) YourElement(name, configFile, dieIndex);
  }
  ```
  在相应 CMake/注册表中声明元素名与工厂符号，便于 `pl_launch` 通过 `-e/-c` 参数加载

示例骨架：
```cpp
class MyElement : public CElement {
public:
    MyElement(const char* n, const char* cfg, int die)
      : CElement(n, cfg, die, VIDEO_DECODER) {}

    app_ret Init() override {
        // 解析配置、创建队列/池/设备句柄
        return APP_SUCCESS;
    }
    app_ret Start() override {
        // 若需要，创建工作线程
        return APP_SUCCESS;
    }
    app_ret ProcessData(CBaseMeta* meta, CElement const* prev) override {
        // 读取 meta（FRAME_META/ BATCH_META），执行业务
        // ...
        return APP_SUCCESS;
    }
    app_ret Finish() override {
        // 释放资源
        return APP_SUCCESS;
    }
};
```

---

## 5. 与 launcher/case 脚本的协作

- `pl_launch`/`espl_launch` 会按注册的元素名和工厂函数创建元素并组织管线：
  - 通过 `-e <ElementName> -c <ConfigYAML>` 顺序装配元素，并调用 `AddToPipeline` 与 `LinkMany`
  - 你的元素只需遵循 `CElement` 约定，无需关注外层装配细节
- `pipeline/case/*` 提供了多个场景脚本，展示如何用 `-e/-c` 组合出不同的业务链路（解码→推理→OSD→显示等）

---

## 6. 进阶实践与最佳实践

- 批处理与多输入
  - 针对多路/批量可使用 `CBatchMeta` 汇聚；在 `ProcessData` 中优先判断 `baseMeta->mMetaType` 并做分支处理
- 性能与时延
  - 关键路径注意零拷贝与池化；结合 `PerformanceStatic` 定期输出耗时分布
  - 统一使用 `BlockQueue` 做生产者/消费者同步，必要时设置容量避免内存峰值
- 可靠性
  - 所有 `release()` 路径必须幂等且覆盖异常分支；引用计数务必在所有分支对称
  - 设备/句柄的 `Init/Finish` 对应严格一一匹配
- NUMA/多 DIE
  - `dieIndex` 贯穿元素与 Meta；结合 `set_thread_affinity(die)`、`MetaPool(die)`、VB 分区减少跨 DIE 访存

---

## 7. 参考文件
- `src/core/include/pipeline.h` / `src/core/src/pipeline.cpp`
- `src/core/include/element.h`
- `src/core/include/base_meta.h` / `batch_meta.h` / `video.h`
- `src/core/include/queue.h` / `pool.h`
- `src/core/include/perf_tools.h` / `sw_performance.h`
- `pipeline/src/pl_launch/pl_launch.cpp`
- `pipeline/case/*`

如需我基于你当前业务，输出“新元素骨架 + CMake + 示例 YAML”的起步包，请告诉我元素用途与上下游接口（输入/输出 Meta 类型、是否异步）。
