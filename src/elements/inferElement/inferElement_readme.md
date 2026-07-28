# inferElement_readme

`inferElement` 负责模型推理，通常接在预处理之后，输出检测/分类/分割等结果 Meta。

## 0. 工程位置与上下游
- 源码路径：`pipeline/src/elements/inferElement/`
- 关键文件：`inferElement.h/.cpp`、`npuUtils.h/.cpp`、`inferElement_config.yml`
- 上游元素：`preProcessElement`（提供模型输入格式的图像/批处理）
- 下游元素：`postprocessElement`、`trackerElement` 或其他自定义消费者

## 1. 作用与数据流
- 接收图像/批处理输入（来自 `preProcessElement`）
- 调用 SDK 推理 API（如 `ES_` 推理接口），获得结果
- 结果封装并下发至后处理 `postprocessElement` 或跟踪 `trackerElement`

### 1.1 核心代码逻辑（源码映射）
- 初始化阶段（`InferElement::Init`）
  - 解析 YAML（`parseConfigFile`）：校验 `model-filepath`，读取 `unique-id/die-id/inferOutputPoolSize/dumpflag/isAsync`；
  - 创建 NPU 环境（`NpuContextUtils::preparation`）：`ES_NPU_SetDevice` → `ES_NPU_CreateContext/ES_NPU_GetCurrentContext`；
  - 加载模型（`NpuUtils::loadModel`）：`ES_NPU_LoadModelFromFile`，查询张量描述 `ES_NPU_Get{Input,Output}TensorDesc`；
  - 创建推理输出内存池（`PL_ES_VB_CreatePool`），为每个输出张量分配池。
- 运行阶段（`InferElement::Start/ProcessData`）
  - `Start`：若异步模式，启动线程 `attachInferOutputMetaAsync` 用于收集回调并下发；
  - `ProcessData`：为每帧/批
    1) 组装输入/输出 DMA FD（输入来自预处理，输出从内存池 `PL_ES_VB_GetBlock` 申请）；
    2) 构造 `NPU_TASK_S`（`NpuUtils::prepareAndCheckData`）；
    3) 同步：`ES_NPU_Submit` 后直接发布；异步：`ES_NPU_SubmitAsync`，回调在查询线程中通过 `ES_NPU_ProcessReport` 驱动；
    4) 将推理结果写入 `CInferOutputMeta` 并 `TransMitToNextToProcess` 下游；支持 `dumpflag` 时用 `ES_SYS_Mmap/Munmap` 转储调试。
- 等待与收尾（`InferElement::Wait/Finish`）
  - `Wait`：异步模式等待回传线程退出并记录性能统计；
  - `Finish`：释放模型 `ES_NPU_UnloadModel`、销毁上下文 `ES_NPU_DestroyContext/ES_NPU_ReleaseDevice`、销毁内存池 `PL_ES_VB_DestroyPool`。

## 2. ES_ SDK API 分类罗列
- 常用能力：设备与上下文管理、模型加载、张量描述查询、提交任务（同步/异步）、异步报告处理。
- 本元素源码实际使用到的 ES_ API（与 `npuUtils.cpp` 保持一致）：
  - 设备/上下文：
    - `ES_NPU_GetNumDevices`
    - `ES_NPU_SetDevice`
    - `ES_NPU_ReleaseDevice`
    - `ES_NPU_CreateContext`
    - `ES_NPU_GetCurrentContext`
    - `ES_NPU_SetCurrentContext`
    - `ES_NPU_DestroyContext`
  - 模型与张量：
    - `ES_NPU_LoadModelFromFile`
    - `ES_NPU_UnloadModel`
    - `ES_NPU_GetNumInputTensors`
    - `ES_NPU_GetNumOutputTensors`
    - `ES_NPU_GetInputTensorDesc`
    - `ES_NPU_GetOutputTensorDesc`
  - 任务提交与流：
    - `ES_NPU_CreateStream`
    - `ES_NPU_Submit`（同步）
    - `ES_NPU_SubmitAsync`（异步）
    - `ES_NPU_ProcessReport`（配合异步线程/信号量）
  - 内存与视频缓冲（结果缓冲与调试转储）：
    - `ES_SYS_Mmap`、`ES_SYS_Munmap`
    - `PL_ES_VB_CreatePool`、`PL_ES_VB_GetBlock`、`PL_ES_VB_DestroyPool`
    - 说明：内存池封装内部还可能调用 `ES_VB_AllocIOVA`（仅在 `PL_ES_VB_CreatePool(..., bisGetIOVA=true)` 情形下），通常对 `inferElement` 使用透明。
  - 常见类型/结构：`ES_S32`、`ES_U64`、`ES_CHAR`、`NPU_TASK_S`、`npu_context`、`npu_stream`。
  - 与解码的关系：解码由 `vdecElement`（如 `ES_VDEC_GetFrame`）完成；`inferElement` 消费上游数据，不直接调用 VDEC 接口。

### 2.1 PL_ES_ 封装到 ES_ SDK 的映射
`PL_ES_` 前缀是 pipeline/core 对底层 ES SDK 的轻量封装，便于统一日志与参数。与本元素相关的内存池封装位于：
- 路径：`pipeline/src/core/src/pl_mem_wrap.cpp`

映射关系如下（左为封装，右为真实 SDK）：
- `PL_ES_VB_CreatePool(...)` → `ES_VB_CreatePool(...)`
- `PL_ES_VB_GetBlock(...)` → `ES_VB_GetBlock(...)`
- `PL_ES_VB_ReleaseBlock(...)` → `ES_VB_ReleaseBlock(...)`
- `PL_ES_VB_DestroyPool(...)` → `ES_VB_DestroyPool(...)`

在 `inferElement.cpp` 中，推理输出的转储调试会直接使用系统映射接口：`ES_SYS_Mmap/ES_SYS_Munmap`（受 `dumpflag` 控制）。

### 代码级调用示例（伪代码）
```cpp
// 1) 设备与上下文
ES_NPU_GetNumDevices(&num);
ES_NPU_SetDevice(cfg.die_id);
ES_NPU_CreateContext(&ctx, cfg.die_id);
ES_NPU_SetCurrentContext(ctx);

// 2) 加载模型并查询张量描述
ES_NPU_LoadModelFromFile(&modelId, (ES_CHAR*)cfg.model_filepath.c_str());
ES_NPU_GetNumInputTensors(modelId, &numIn);
ES_NPU_GetNumOutputTensors(modelId, &numOut);
for (int i=0;i<numIn;++i)  ES_NPU_GetInputTensorDesc(modelId,  i, &inDesc[i]);
for (int i=0;i<numOut;++i) ES_NPU_GetOutputTensorDesc(modelId, i, &outDesc[i]);

// 3) 异步模式：创建流并起报告处理线程
if (cfg.isAsync) {
    ES_NPU_CreateStream(&stream);
    StartReportThread(stream); // 线程内循环 ES_NPU_ProcessReport(stream, -1)
}

// 4) 组装 NPU_TASK_S，填充输入/输出 DMA fd，并提交
auto task = BuildTaskFromFrame(frame, modelId, inDesc, outDesc, callback, cbArg);
if (cfg.isAsync) {
    ES_NPU_SubmitAsync(task, 1, stream);
    // 回调在报告线程触发；在回调中发布结果到下游
} else {
    ES_NPU_Submit(task, 1);
    PublishResult(task); // 同步路径直接发布
}

// 5) 释放资源（Stop/Finish）
ES_NPU_UnloadModel(modelId);
ES_NPU_DestroyContext(ctx);
ES_NPU_ReleaseDevice(cfg.die_id);
```

## 3. 配置（示例）
```yaml
model-filepath: "resnet50-sim.model"
unique-id: 1
die-id: 0
inferOutputPoolSize: 3
dumpflag: 0
isAsync: 1
```

- 参数说明
  - `model-filepath` 必填：模型文件路径。
  - `unique-id` 可选：元素实例 ID。
  - `die-id` 可选：芯片/Die 编号，超出范围会被重置为默认值（通常 0）。
  - `inferOutputPoolSize` 可选：输出内存池大小，≥1，默认 3。
  - `dumpflag` 可选：调试转储开关，0 关闭。
  - `isAsync` 可选：是否异步推理（1 异步，0 同步）。
- 兼容性与注意事项
  - 历史参数名 `inferType` 已被替换为 `isAsync`，请统一改为 `isAsync`。
  - 请确保补充 `dumpflag`，否则调试转储功能不可用。
  - 当 `inferOutputPoolSize < 1` 时会视为非法并导致初始化失败。
  - `die-id` 合法范围通常为 [0,1]；超出范围时会被重置为 0。

## 4. 使用方式（两种）
- 方式一：编码构建 Pipeline（参考 `pipeline/src/pl_launch/pl_launch.cpp` 的 `main/option_parser`）
  - 流程：解析 YAML → 创建元素 → `AddToPipeline` → `Link/LinkMany` → `Start`。
  - 直接创建推理元素（与 `pl_launch` 加载一致的工厂符号）：
```cpp
auto inferCfg = LoadYaml("pipeline/src/elements/inferElement/infer_config.yml");
auto inferEl = ElementFactory::Create("inferElement", inferCfg);
pipeline->Add(inferEl);
pipeline->Link(preProcessEl, inferEl);
pipeline->Link(inferEl, postprocessEl /*或 trackerEl*/);
pipeline->Start();
RunMainLoop();
```

  - 或使用 so 导出工厂函数：
```cpp
// 对应 so 中导出的工厂符号：createEsInferElement
extern "C" CElement* createEsInferElement(const char* name, const char* configFile, int dieIndex);

auto* inferEl = createEsInferElement("infer1", 
    "pipeline/src/elements/inferElement/infer_config.yml", /*dieIndex=*/0);
pipeline->AddToPipeline(inferEl, /*prev=*/preProcessEl);
pipeline->LinkMany(inferEl, postProcessEl, nullptr);
```

- 方式二：通过 `espl_launch` 可执行程序参数化构建（参考 `pipeline/case/*` 各脚本）
  - 单路 OD：`pipeline/case/od/od_pipeline_release.sh`
  - 多阶段推理：`pipeline/case/msi/*`
  - 双 DIE：`pipeline/case/dualDie/*`

```sh
# 片段摘自 od_pipeline_release.sh，展示 infer 节点串联方式
espl_launch perfstat_interval 10000000 config_path $case_path/config/ \
  EsAvDemux -path EsAvDemux_2_release.yaml -loopnum $cloopnum - ! \
  EsVdec -name decoder1 -path EsVdec.yaml - ! \
  EsMux -name mux1 -timeout 40 -poolsize 16 - \
  ! EsPreProcess -name preproc1 -path EsPreProcess.yaml - \
  ! EsInfer -name infer1 -path EsInfer.yaml - \
  ! EsPostProcess -name post1 -path EsPostProcess.yaml - \
  ! EsVideoSink -name vo1 -path EsVideoSink.yaml -
```
在上述链路中，`EsInfer` 的 `-path` 指向其 YAML（例如 `EsInfer.yaml` 或 `infer_config.yml`）。
  - 快速模板（将路径替换为你的 case 路径）：
```sh
espl_launch config_path /abs/path/to/case/config/ \
  EsPreProcess -name pre -path EsPreProcess.yaml - \
  ! EsInfer -name infer -path EsInfer.yaml - \
  ! EsPostProcess -name post -path EsPostProcess.yaml -
```

提示：脚本中 `config_path` 会作为各元素 `-path` YAML 的前缀目录，`-` 与 `!` 用于描述流水线的链接关系（见 `pl_launch.cpp` 的 `option_parser`）。

## 5. 二次开发
- 常见改动点
  - 自定义批大小、内存池策略（降低拷贝、提升复用）。
  - 同/异步切换与回调处理（队列化输出、跨线程发布）。
  - 模型切换/多模型路由（按流/按场景选择不同模型）。
  - 前后处理对齐：像素格式、尺度、均值方差、NMS/阈值等。
- 修改建议
  - 在 `Init/Start` 阶段完成模型与上下文准备；`Process` 中仅做轻量逻辑。
  - 为关键路径添加日志与断言（模型路径、输入尺寸、Tensor 排布）。
  - 用开关保护高开销调试逻辑（如 `dumpflag`）。
 - 自定义插件步骤（概览）
   1) 复制本元素为模板，替换类名与导出符号（例如 `createEsMyInferElement`）；
   2) 在 `parseConfigFile` 中增加你的参数解析；
   3) 在 `ProcessData` 中对接你模型的输入/输出组织；
   4) 导出 so 并在 `pl_launch.cpp` 的参数解析中新增分支或通过脚本直接引用新 so；
   5) 提供对应 YAML 与 `case` 示例脚本，便于联调。

## 6. 排障
- 常见问题
  - 模型路径错误/权限不足：确认 `model-filepath` 与运行目录。
  - 设备内存不足：调小 `inferOutputPoolSize` 或分辨率/批大小。
  - 维度或格式不匹配：确保预处理输出与模型输入一致（尺寸、通道、layout）。
  - 参数名不一致：确保使用 `isAsync` 而非历史的 `inferType`。
  - 异步回调未触发：检查队列、回调绑定、线程安全问题。
  - 性能不达标：开启异步、增大并行度、使用内存池、避免多余 memcpy。

## 7. 源码导航与快速索引
- `inferElement.h/.cpp`
  - 入口函数：`Init`（上下文/模型/内存池）、`Start`（异步线程）、`ProcessData`（提交任务/拼装输出）、`Wait/Finish`（统计/释放）。
  - 回调与异步：`attachInferOutputMetaAsync`、`taskCallBack`、`ES_NPU_ProcessReport` 驱动。
  - EOS 处理：`ProcessData` 中基于 `batchMeta->eosFlag` 的收尾逻辑。
- `npuUtils.h/.cpp`
  - 上下文：`NpuContextUtils::{preparation,setContext,releaseContext}` 封装了 `ES_NPU_*` 设备/上下文 API。
  - 模型与任务：`NpuUtils::{loadModel,submitSync,submitAsync,releaseModel}`；`prepareAndCheckData` 组装 `NPU_TASK_S`。
- `core/src/pl_mem_wrap.cpp`
  - `PL_ES_VB_{CreatePool,GetBlock,ReleaseBlock,DestroyPool}` 对 `ES_VB_*` 的轻封装，内部可能申请 IOVA。
- `src/pl_launch/pl_launch.cpp`
  - 通过 `dlopen + dlsym` 动态装载各元素，工厂符号：`createEsInferElement(const char* name, const char* configFile, int dieIndex)`。
