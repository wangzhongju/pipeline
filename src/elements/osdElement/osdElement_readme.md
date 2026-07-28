# osdElement_readme

本文档从使用与二次开发者角度出发，帮助你：
- 快速理解 `osdElement` 的职责与核心代码逻辑；
- 熟悉本元素涉及或关联到的真实 SDK API 触点；
- 提供两种使用方式：编码方式构建 Pipeline、通过 `espl_launch` 命令参数构建；
- 指导如何基于源码定制自己的 OSD 类插件。

---

## 1. 功能与定位
`osdElement` 用于在视频帧上绘制多种叠加信息（OSD）：
- 目标框与文字（常用于检测/跟踪结果显示）。
- 时间戳/日期叠加。
- 稳定水印文字。
- 姿态关键点/骨架连线（RTMPose 可视化）。

输入输出：
- 输入：
  - 单路 `CFrameMeta`，或由上游合批后的 `CBatchMeta`（逐帧处理）。
  - 或输入 `CVideoGridMeta`（将栅格拼接后的整体帧进行 OSD）。
- 输出：
  - 同步在原视频帧缓冲上绘制（in-place），继续传给下游（通常是显示、编码或文件落盘）。

该元素不做编解码，仅对图像帧做图形渲染。内部抽象 `OsdProc` 封装了 CPU/GPU/Freetype/GLES 等实现细节（见 `src/elements/osdElement/common`）。

---

## 2. 核心代码逻辑
源码位置：`pipeline/src/elements/osdElement/`
- 头文件：`osdElement.h`
- 源文件：`osdElement.cpp`
- 配置：`osd.yaml`（样例）、`osdElement_config.yml`（带注释说明）
- YAML 解析：`yaml_parser.cpp/.h`

关键成员：
- `OSD_PARAM_S mOsdParam`：由 `parse_config_file(&mOsdParam, m_configFile)` 解析得到，包含文本、矩形框、日期、稳定水印、RTMPose、dump 等参数。
- `OsdProc* mOsdProc`：OSD 实现对象（prepare/process/unprepare 等）。
- `dx::Debugger mDbg`：可选输入/输出帧 dump。
- `PerformanceStatic* mOsdPerformance`：性能统计。

主要流程：
- `Init()`：
  - 要求上/下游均为单路（`m_PreviousElementVec.size() == 1 && m_NextElementVec.size() == 1`）。
  - 解析 YAML 到 `mOsdParam`，初始化性能统计与基准时间。
- `Start()`：
  - 按配置计算文字与矩形的颜色参数（`textColor`、`rectColor`、`RtmOsdColor`）。
- `ProcessData(baseMeta, previousElement)`：
  - 处理三种 Meta：
    - `CBatchMeta`：循环取每帧，调用 `ProcessFrameMeta`。
    - `CFrameMeta`：直接 `ProcessFrameMeta`。
    - `CVideoGridMeta`：先 `ProcessGridMeta` 将网格复原为一帧再 `ProcessFrameMeta`。
  - `eosFlag` 直接透传（不绘制）。
- `ProcessFrameMeta(frameMeta)`：
  1) 选取绘制通道：`frameMeta->images[channelID]`（`channelID` 来自配置）。
  2) 可选 dump 输入帧（`mOsdParam.input_dump_enable`）。
  3) `mOsdProc->prepareOsd(videoFrame, w, h)`。
  4) 目标框与文字：
     - 遍历 `frameMeta->objs`（检测/跟踪结果），优先使用跟踪框（若存在），否则检测框；
     - 归一化坐标保护（确保在 [0,1] 范围）；
     - 生成矩形绘制列表 `rectRects` 与文字区域 `textRects`，构造类目+ID 与置信度文本；
     - 若启用文本：`settextparam(..., fontHeight, textColor)` 后 `process()`；
     - 若启用矩形：`setrectparam(..., pensize)` 后 `process()`；
     - 记录性能段统计（对象数）。
  5) RTMPose 关键点/骨架：
     - 遍历 `frameMeta->rtmObjs` 的 `keyPoint`，根据配置的关键点集合与连线对生成点/线列表；
     - `setRtmOsdParam(rects, rectNum, points, pointNum, RtmOsdColor, pensize)` 后 `process()`。
  6) 日期叠加：
     - 基于 `baseTime` 与 `pts` 生成字符串，按配置位置绘制；
  7) 稳定水印：
     - 使用 `stabletext` 与 `stablepos` 绘制固定文本。
  8) `mOsdProc->unprepareOsd(videoFrame)`；
  9) 可选 dump 输出帧（`mOsdParam.output_dump_enable`）。
- `ProcessGridMeta(gridMeta)`：
  - 构造 `VIDEO_FRAME_INFO_S`，将 `CVideoGridMeta` 的面片信息映射到单帧坐标系；
  - 对每个原始对象坐标做 `rect_adjust`（子区域坐标 -> 全图坐标），生成新的 `CFrameMeta` 后调用 `ProcessFrameMeta`；
  - 归还资源。
- `perfStat()`：输出 OSD 性能统计；`Finish()`：释放资源。

设计要点：
- 所有坐标均按 [0,1] 归一化，OSD 内部根据帧尺寸换算像素。
- 文字高度 `fontheight` 为比例值（相对高度），更适合多分辨率场景。
- 渲染在输入帧上完成，避免额外拷贝。

---

## 3. 与 SDK API 的关系（仅列 ES_ 前缀真实 SDK）
`osdElement` 自身主要调用工程内部的 OSD 渲染封装（`OsdProc`），并未直接调用多媒体/推理类 `ES_` API。但在整体工程中，系统初始化与公共缓冲管理等真实 `ES_` API 由 core 层统一调用。为帮助你建立全局认知，列举如下：

3.1 Pipeline 生命周期（core 层，见 `pipeline/src/core/src/pipeline.cpp` 等）
- 日志与系统：
  - `ES_SYS_SetLogCfgPath`
  - `ES_SYS_Init`
  - `ES_SYS_Exit`
- VB（公共视频缓冲）配置与初始化：
  - `ES_VB_GetConfig`, `ES_VB_SetConfig`, `ES_VB_Init`
- VB 资源管理：
  - `ES_VB_CreatePool`, `ES_VB_GetBlock`, `ES_VB_ReleaseBlock`, `ES_VB_DestroyPool`
  - 可选：`ES_VB_AllocIOVA`

说明：对于工程中出现的 `PL_ES_*` 包装（如 `pipeline/src/core/src/pl_mem_wrap.cpp`），请在对外文档中统一用真实 `ES_*` 名称指代。

3.2 与 osdElement 常见上下游模块（串联参考）
- 解码：`ES_VDEC_SendStream`, `ES_VDEC_GetFrame`, `ES_VDEC_ReleaseFrame`
- 推理：`ES_NPU_LoadModelFromFile` 等（用于产出 `objs/rtmObjs` 的元素）
- 编码/显示：`ES_VENC_*`、显示相关 `ES_` API（视平台而定）

---

## 4. 配置与参数
示例：`osd.yaml`（case 目录亦有 `EsOsd.yaml` 示例）
```yaml
die-id: 0
text:
  enable: true
  fontfile: /opt/demo/pipeline/dataset/font/SourceHanSansCN-Normal.ttf
  pencolor: [0,255,0]
  fontheight: 0.03
rect:
  enable: true
  pencolor: [255,0,0]
  pensize: 4
date:
  enable: false
stable:
  enable: true
  stabletext: Eswin
  stablepos: [0.5, 0.5]
rtmpose:
  enable: false
  # 若启用，可进一步配置 body/face/hand/foot 的关键点集合与 keypointConnect 连线
  # body: [..]
  # keypointConnect: [[5,6],[6,8], ...]
dump:
  inputenable: false
  outputenable: false
```
关键点：
- 颜色配置由 `yaml_parser.cpp` 解析进 `mOsdParam.textcolor/rectcolor/rtmOsdParam.rectcolor`（RGB），内部转为 `Color` 结构。
- `fontheight` 为相对高度比例；
- `stablepos` 为归一化坐标 `[x,y]`；
- 选择绘制图层的通道 `channelId` 由 `mOsdParam.channelId`（在解析结构中）控制。

---

## 5. 两种使用方式

5.1 通过编码方式构建 Pipeline
```c++
// 工厂接口（已导出）
extern "C" CElement* createEsOsdElement(const char* name, const char* path, int dieIndex);

// 创建并加入流水线
auto* osd = createEsOsdElement("EsOsd0", "/abs/path/to/osd.yaml", 0);
pipe->AddToPipeline(osd, nullptr);

// 典型链路：解码 -> OSD -> 编码/显示/落盘
pipe->LinkMany(vdec0, osd, nullptr);
pipe->LinkMany(osd, venc0, nullptr); // 或 fileSink/display 等
```
要点：
- `osdElement` 要求单上游、单下游；
- 上游需提供 `CFrameMeta`，其中 `images[channelId]` 指向要绘制的帧；
- 若上游为 `CBatchMeta`，需要在链路中加上批次拆分类元素（如 `demuxElement`）或直接在 `osdElement` 内部对 `CBatchMeta` 逐帧处理（已支持）。

5.2 通过 `espl_launch` 命令参数构建 Pipeline
- 目前 `src/pl_launch/pl_launch.cpp` 未内置 `EsOsd` 分支。两种方案：
  1) 扩展 `pl_launch.cpp`：增加 `EsOsd` 的参数解析与 `dlopen` 逻辑，调用 `createEsOsdElement` 创建并加入；
  2) 在你的业务进程中采用“编码方式”（见 5.1）。
- case 参考：`pipeline/case/**/config/EsOsd.yaml` 多处示例可直接复用。

命令模板（完成 `pl_launch` 扩展后可用）：
```bash
espl_launch \
  config_path /path/to/config \
  EsAvDemux -path EsAvDemux.yaml -die 0 ! \
  EsVdec    -path EsVdec.yaml    -die 0 ! \
  EsOsd     -path EsOsd.yaml     -die 0 ! \
  EsVenc    -path EsVenc.yaml    -die 0 !
```

---

## 6. 二次开发：如何基于源码定制 OSD 插件
1) 继承基类与接口实现
- 基类：`CElement`
- 建议实现：`Init/Start/ProcessData/perfStat/Finish/InfoQuery`；
- 导出工厂函数（便于 `dlopen`）：
```c++
extern "C" CElement* createEsYourOsd(const char* name, const char* path, int dieIndex);
```

2) 渲染实现与性能
- 根据平台可在 `OsdProc` 中切换 CPU/GPU/2D/GLES 后端；
- 批量绘制时尽量合并图元设置，减少多次 `process()`；
- 对关键路径使用 `PerformanceStatic` 打点；

3) 数据与坐标规范
- 均采用归一化坐标 [0,1]；
- 多图层叠加顺序：文字 -> 框 -> 关键点/连线（或按业务定制）；
- 注意 `reduceUseCount()` 的时机，避免泄漏；

4) 与 SDK 的交互规范
- 文档与示例中仅暴露真实 `ES_` API 名称；
- 系统/VB 等公共资源在 core 层统一以 `ES_SYS_*`、`ES_VB_*` 管理；
- 若你的插件涉及解码/编码/显示/推理，请直接引用对应模块 `ES_` API（如 `ES_VDEC_*`、`ES_VENC_*`、`ES_NPU_*`）。

---

## 7. 参考文件
- `pipeline/src/elements/osdElement/osdElement.h/.cpp`
- `pipeline/src/elements/osdElement/common/*.cpp`（OSD 后端实现：CPU/GPU/2D/GLES/Freetype）
- `pipeline/src/elements/osdElement/yaml_parser.cpp/.h`
- `pipeline/src/elements/osdElement/osd.yaml`、`pipeline/case/**/config/EsOsd.yaml`
- `pipeline/src/core/src/pipeline.cpp`、`pipeline/src/core/src/pl_mem_wrap.cpp`（`ES_*` 生命周期与 VB 管理）
- `pipeline/src/pl_launch/pl_launch.cpp`（可参考其为其他元素的装载方式扩展 `EsOsd`）
