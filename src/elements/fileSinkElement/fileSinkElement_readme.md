# fileSinkElement_readme

本说明面向使用与二次开发者，帮助你：
- 快速理解 `fileSinkElement` 的核心代码逻辑和在 Pipeline 中的职责；
- 熟悉与本 element 相关的真实 SDK API 触点；
- 两种使用方式：通过编码方式构建 Pipeline、通过 `espl_launch` 命令参数构建；
- 基于源码定制自己的 element 插件。

---

## 1. 功能与定位
`fileSinkElement` 用于把上游传来的编码码流数据写入文件，便于落盘调试与数据留存：
- 输入：`CVideoPacketMeta`（其中承载编码后的视频码流 `VENC_STREAM_S`）。
- 支持类型（由输入动态决定）：H.264、H.265、JPEG。
- 输出：
  - H.264/H.265：将持续写入同一个文件（按前缀+element名+扩展名）；在 `eos` 时关闭文件。
  - JPEG：每帧一个独立图片文件，文件名自动带 10 位序号。

该 element 不负责编解码，也不直接使用多媒体 SDK 的解码/编码接口；它只是将上游已经编码好的码流原样保存到磁盘。

---

## 2. 核心代码逻辑
源码位置：`pipeline/src/elements/fileSinkElement/`
- 头文件：`fileSinkElement.h`
- 源文件：`fileSinkElement.cpp`
- 配置文件：`fileSinkElement_config.yml`

关键成员与流程：
- 初始化 `Init()`
  - 解析 YAML：根键 `filesink`，读取 `fileprefix` 作为输出文件前缀；
  - 初始化性能统计 `saveStreamPerformance`。
- 处理 `ProcessData(CBaseMeta* baseMeta, const CElement* previousElement)`
  - 将 `baseMeta` 视为 `CVideoPacketMeta`，取出 `encVideoPkt: VENC_STREAM_S*`；
  - 遇到 `eosFlag`：
    - 若是 H.264/H.265 模式且文件已打开，关闭文件；
    - 记录日志后返回；
  - 首帧时：根据 `pVideoPacketMeta->type` 决定 `payloadType` 并生成保存文件名：
    - H.264/H.265：`<fileprefix>_<elementName>.h264/.h265`，打开单一输出文件；
    - JPEG：基名 `<fileprefix>_<elementName>.jpg`，每帧落盘为 `<purename>_<%010d>.jpg`；
  - 写盘：
    - JPEG：逐帧单文件 `saveStreamJpeg`；
    - 其他：追加写入单一文件 `saveStream`；
  - 计数与释放：帧用毕后 `pVideoPacketMeta->reduceUseCount()`，并做性能段统计。
- 结束与统计
  - `perfStat()` 输出统计；`Finish()` 释放性能统计对象。

辅助函数：
- `videoPt2Str()` 将 `PAYLOAD_TYPE_E` 转为扩展名（h264/h265/jpg）。
- `stringFindLastOf()`、`saveStream()`、`saveStreamJpeg()` 提供命名和落盘细节。

---

## 3. 与 SDK API 的关系（仅列出 ES_ 前缀的真实 SDK）
`fileSinkElement` 自身不直接调用 `ES_` 编解码 API；它接收的是已经编码好的 `VENC_STREAM_S` 码流并做文件写入。

为便于你理解整条 pipeline 与 SDK 的接触点，这里列出与本工程相关、但通常在 core 或其他 element 中出现的真实 `ES_` API：

3.1 Pipeline 生命周期（core 层，见 `pipeline/src/core/src/pipeline.cpp`）
- 系统与日志：
  - `ES_SYS_SetLogCfgPath`
  - `ES_SYS_Init`
  - `ES_SYS_Exit`
- 公共缓冲区（VB）配置与初始化：
  - `ES_VB_GetConfig`, `ES_VB_SetConfig`, `ES_VB_Init`
- VB 资源管理：
  - `ES_VB_CreatePool`, `ES_VB_GetBlock`, `ES_VB_ReleaseBlock`, `ES_VB_DestroyPool`
  - 可选：`ES_VB_AllocIOVA`

说明：core 中可能存在 `PL_ES_` 封装（例如 `src/core/src/pl_mem_wrap.cpp`），但本 README 不展示 `PL_ES_` 名称；它们底层映射到上述真实 `ES_VB_*` 等 SDK API。

3.2 典型上游/下游涉及的 ES_ API（便于串联工程认知）
- 若上游是编码器 element（非本 element 范畴），可能使用：
  - `ES_VENC_*`（发送原始帧、获取编码码流等）
- 若链路中包含解码器/显示/推理等，请参考对应 element 的 README，仅关注 `ES_` 前缀真实 API，如：
  - 解码：`ES_VDEC_SendStream`, `ES_VDEC_GetFrame`, `ES_VDEC_ReleaseFrame`
  - 推理：`ES_NPU_LoadModelFromFile` 等

---

## 4. 配置与参数
配置示例：`fileSinkElement_config.yml`
```yaml
filesink:
  # 输出文件前缀，实际文件名由实现自动补后缀/序号
  fileprefix: output
  # filetype: 0  # 若版本支持：0-video、1-jpeg（当前实现未读取该键）
```
- `fileprefix`：必须，影响输出文件名：
  - H.264/H.265：`<fileprefix>_<elementName>.h264/.h265`
  - JPEG：`<fileprefix>_<elementName>_%010d.jpg`
- 输入类型由上游决定（`CVideoPacketMeta::type`）。

---

## 5. 两种使用方式

5.1 通过编码方式构建 Pipeline
- 使用工厂函数创建并加入流水线：
```c++
extern "C" CElement* createEsFileSinkElement(const char* name, const char* path);
CElement* sink = createEsFileSinkElement("EsFileSink0", "/abs/path/to/fileSinkElement_config.yml");
pipe->AddToPipeline(sink, nullptr);
// 与上游（如编码器/复用器等）连接
pipe->LinkMany(prev, sink, nullptr);
```
- 运行时，上游应提供 `CVideoPacketMeta`，并设置好 `encVideoPkt` 与 `type`。

5.2 通过 `espl_launch` 命令参数构建 Pipeline
- 现有 `src/pl_launch/pl_launch.cpp` 中未内置 `EsFileSink` 分支（未像 `EsAvDemux`/`EsVdec` 那样通过 `dlopen` 加载）。你有两种选择：
  1) 扩展 `pl_launch.cpp`：仿照 `EsAvDemux` 的实现，新增 `EsFileSink` 分支，`dlopen` 你的 `libes_plfilesink.so` 并调用工厂 `createEsFileSinkElement`；
  2) 在你的应用进程中采用“编码方式”（见 5.1）组装链路。
- 若选择方案 1，脚本可参考 `pipeline/case` 下的示例配置：
  - `pipeline/case/codec/config/EsFileSink.yaml` 等；
- 命令行模板（完成 `pl_launch` 扩展后可用）：
```bash
espl_launch \
  config_path /path/to/pipeline/case/codec/config \
  EsAvDemux  -path EsAvDemux.yaml -loopnum 1 -die 0 ! \
  EsVdec     -path EsVdec.yaml    -die 0 ! \
  EsFileSink -path EsFileSink.yaml !
```

---

## 6. 二次开发：如何基于源码定制自己的 element 插件
1) 继承基类并实现接口
- 基类：`CElement`
- 建议实现：`Init/Start/Wait/ProcessData/ProcessAndTransmit/Finish/perfStat/InfoQuery`；
- 提供 C 接口工厂，便于 `dlopen`：
```c++
extern "C" CElement* createEsYourElement(const char* name, const char* path, int dieIndex);
```

2) 与 SDK 的交互规范
- 文档中仅展示真实 `ES_` API 名称；
- 若需要公共内存池/块：`ES_VB_CreatePool/ES_VB_GetBlock/ES_VB_ReleaseBlock/ES_VB_DestroyPool`（必要时 `ES_VB_AllocIOVA`）；
- 若涉及解码/编码/显示/推理，使用对应模块 `ES_` API（如 `ES_VDEC_*`、`ES_VENC_*`、`ES_NPU_*`）。

3) 文件落盘细节与健壮性
- H.264/H.265 建议在 `eos` 时关闭文件句柄，异常路径注意判空；
- JPEG 文件命名包含 10 位序号，便于排序；
- 注意 `reduceUseCount()` 的引用计数释放，避免内存泄漏；
- 使用 `PerformanceStatic` 做段时延统计，必要时增加写盘耗时打点与错误处理。

4) NUMA/亲和与日志
- 可参考其它元素的做法，通过 `-die` 参数在 `pl_launch` 层绑定线程亲和；
- 日志通过 `app_info/app_warn/app_error` 等宏输出；系统日志路径/等级由 core 的 `ES_SYS_*` 初始化控制。

---

## 7. 参考文件
- `pipeline/src/elements/fileSinkElement/fileSinkElement.h/.cpp`
- `pipeline/src/elements/fileSinkElement/fileSinkElement_config.yml`
- `pipeline/src/pl_launch/pl_launch.cpp`（可参考其为其他元素的装载方式扩展 `EsFileSink`）
- `pipeline/src/core/src/pipeline.cpp`、`pipeline/src/core/src/pl_mem_wrap.cpp`（`ES_*` 生命周期与 VB 管理）
- `pipeline/case/**/config/EsFileSink.yaml`
