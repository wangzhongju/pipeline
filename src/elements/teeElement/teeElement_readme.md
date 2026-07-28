# teeElement_readme

本文档面向使用者与二次开发者，帮助你：
- 快速理解 `teeElement` 的职责与核心逻辑；
- 熟悉本元素涉及的真实 SDK API 触点；
- 提供两种使用方式：编码方式构建 Pipeline 与通过 `espl_launch` 参数构建；
- 指导如何基于源码定制自己的分流（tee）插件，以满足业务需求。

---

## 1. 功能与定位
`teeElement` 是一个“分流器”，用于将一路上游数据同时分发到多个下游元素。其设计目标：
- 在不拷贝数据的前提下，实现多路下游共享同一份 `CBaseMeta` 数据；
- 通过引用计数与线程安全机制，确保生命周期与并发安全；
- 作为轻量级分支节点，方便在同一条媒体流上并行执行不同处理链路（例如：一支走实时检测，一支走录制/回传）。

输入/输出：
- 输入：任意继承 `CBaseMeta` 的元数据（例如 `CBatchMeta`/`CFrameMeta` 等）。
- 输出：扇出到 `m_NextElementVec` 的所有下游元素；EOS 将被透传。

典型链路：
- 解码 -> tee -> {预处理->推理->后处理->OSD} + {录制/编码/落盘}

---

## 2. 源码结构与核心流程
目录：`pipeline/src/elements/teeElement/`
- 实现文件：`teeElement.h/.cpp`
- 说明/占位配置：`teeElement_config.yml`（当前无可配置键，便于后续扩展）

关键实现（见 `teeElement.cpp`）：
- `Init()`：约束 1 入 N 出（必须 1 个上游，>=1 个下游）；
- `ProcessData(baseMeta, previous)`：
  - 使用 `std::unique_lock<std::shared_mutex>` 锁住 `baseMeta->sLock`；
  - 读取当前 `useCount`，将其设置为 `m_NextElementVec.size() + nowUseCount - 1`；
    - 语义：将同一份 `baseMeta` 的引用计数增加到“下游分支数”以覆盖后续消费；
  - 解锁后返回，由基类 `CElement` 在管线中推进到各个下游；
- 工厂导出：`createEsTeeElement(const char* name, BASE_ELEMENT_TYPE type)`。

线程模型：
- `teeElement` 本身不创建工作线程，不持有内部队列；
- 仅在 `ProcessData` 时原地调整引用计数，依赖框架分发到各下游；
- 下游的异步/同步由各自元素决定（如需要缓冲请在下游插入 `queueElement`）。

并发与生命周期：
- 通过 `baseMeta->sLock`（`std::shared_mutex`）配合 `useCount` 管理同一份数据在多分支的生命周期；
- 下游元素在消费完成后调用 `reduceUseCount()`，当计数归零时释放底层资源。

---

## 3. 真实 ES_ SDK API 触点（仅列 ES_ 前缀）
`teeElement` 自身不直接调用任何 `ES_` 前缀的 SDK API。其逻辑完全基于工程通用的：
- Core 基础：`CElement` 链路管理、`CBaseMeta` 元数据、`sLock`/`useCount` 生命周期管理；
- C++ 标准库：`std::shared_mutex`、`std::unique_lock` 等。

说明：
- 若在未来扩展 `teeElement` 时引入了 `PL_ES_*` 封装，请继续沿着 `pipeline/src/core/src` 的实现追溯到底层真实 `ES_*` API，并仅在文档中列出 `ES_*` 名称。当前版本未使用任何 `PL_ES_*` 或 `ES_*` API。

---

## 4. 配置与参数
- `teeElement_config.yml`：占位文件。当前源码未读取任何 YAML 键，运行无需附带配置文件。
- tee 的分支数由实际 `LinkMany` 到 `m_NextElementVec` 的个数决定，不通过配置项控制。

如需在 YAML 中描述分支策略（例如按条件路由某些帧到特定分支），可在元素中新增配置解析逻辑，并更新此文件。

---

## 5. 两种使用方式

### 5.1 编码方式构建 Pipeline
```c++
// 工厂接口（已导出）
extern "C" CElement* createEsTeeElement(const char* name, BASE_ELEMENT_TYPE elementType);

// 创建 tee 并加入流水线
auto* tee = createEsTeeElement("EsTee0", VIDEO_DECODER /*或按实际上游类型*/);
pipe->AddToPipeline(tee, nullptr);

// 典型扇出：
pipe->LinkMany(vdec, tee, nullptr);
pipe->LinkMany(tee, branchA_pre, nullptr);
pipe->LinkMany(tee, branchB_pre, nullptr);

// 若分支处理速率差异较大，建议在各分支 tee 后放置 queue：
auto* qA = createEsQueueElement(100, 0, "Q_A", 0);
auto* qB = createEsQueueElement(100, 1, "Q_B", 0); // 丢旧以降时延
pipe->LinkMany(tee, qA, nullptr);
pipe->LinkMany(qA, branchA_pre, nullptr);
pipe->LinkMany(tee, qB, nullptr);
pipe->LinkMany(qB, branchB_pre, nullptr);
```
注意：
- tee 不复制数据，依赖引用计数在多分支共享同一份 `CBaseMeta`；
- 分支差异较大或存在背压时，请在分支入口放置 `queueElement` 进行解耦；
- 任何在分支中对元数据的可变写入，都需要确保线程安全（建议只读访问或采用 per-branch 拷贝/克隆策略）。

### 5.2 通过 `espl_launch` 参数构建
- 当前 `src/pl_launch/pl_launch.cpp` 未内置 `EsTeeElement` 分支。
- 可参照已有元素的写法：在 `main` 解析 `-path/-die` 等参数，调用 `createEsTeeElement`，并在图中将其作为扇出节点插入。

完成扩展后，可用类似命令：
```bash
espl_launch \
  EsAvDemux -path EsAvDemux.yaml -die 0 ! \
  EsVdec    -path EsVdec.yaml    -die 0 ! \
  EsTee     -die 0 ! \
    (branch A) EsPreProcess -path EsPreProcess.yaml -die 0 ! EsInfer -path EsInfer.yaml -die 0 ! EsPostProcess -path EsPostProcess.yaml -die 0 ! \
    (branch B) EsVenc -path EsVenc.yaml -die 0 !
```
说明：上例为概念示意；实际在 `pl_launch` 中需要为 tee 增加子链路解析或采用顺序声明后内部记录分支边界的方式组织图结构。

---

## 6. 二次开发与最佳实践
1) 分支策略扩展
- 条件路由：按 `frameId/channel/classId/score/timestamp` 将不同帧路由到不同分支；
- 分支权重：在负载高时动态关闭/降采样某些分支；
- 广播/单播模式：支持仅路由到某一分支或全部分支；

2) 数据一致性与写时拷贝
- 现有实现是“共享只读”理念：分支应尽量只读访问共享元数据；
- 若某分支需要修改数据，建议：
  - 提供 `clone()`/`shallow_copy()`/`deep_copy()` 能力，按需在进入分支前复制；
  - 或在分支内部新建 `CBaseMeta` 容器承载私有结果，避免改动共享体；

3) 与队列的组合
- tee 不做缓冲/节流；与 `queueElement` 组合形成“tee + queue”可实现分支级 QoS；
- 快分支使用阻塞队列，慢分支使用丢旧队列，平衡实时性与完整性；

4) 观测与调试
- 保留对 `useCount` 的日志打印，便于发现引用计数异常；
- 在 `InfoQuery()` 中（如需要）暴露分支数量、下游名称列表，辅助运维；

5) YAML 配置化
- 若需要在命令行/配置中描述分支，推荐在 `pl_launch` 实现“子图或分支域”的解析；
- 在 `teeElement` 中仅保留轻量逻辑，复杂的图编排放在启动器层完成。

---

## 7. 参考文件
- 元素：`pipeline/src/elements/teeElement/teeElement.h/.cpp`
- 队列：`pipeline/src/elements/queueElement/queueElement.h/.cpp`（配合 tee 做分支缓冲）
- 基类/并发：`pipeline/src/core/include/element.h`（`sLock`、NUMA/亲和性、链路编排接口）
- 启动器：`pipeline/src/pl_launch/pl_launch.cpp`（可参考其它元素的装载方式扩展 `EsTee`）
- 案例：`pipeline/case/**` 下的脚本可作为命令行风格参考
