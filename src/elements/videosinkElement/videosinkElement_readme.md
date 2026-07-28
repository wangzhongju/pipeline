# videoSinkElement_readme

本文档面向使用者与二次开发者，帮助你：
- 快速理解 `videoSinkElement` 的职责与核心逻辑；
- 通过本元素熟悉真实 ES_ SDK API 的调用；
- 提供两种使用方式：1) 通过编码方式构建 Pipeline（参考 `pl_launch.cpp` 的 main），2) 通过 `espl_launch` 及 case 脚本参数快速启动；
- 指导如何基于本元素进行二次开发与定制，满足业务需求。

- 源码目录：`src/elements/videoSinkElement/`
  - 主要文件：`videosinkElement.h`, `videosinkElement.cpp`
  - 配置样例：`videosinkElement_config.yml`, `videosink_config.yml`

---

## 1. 功能与核心逻辑
`videoSinkElement` 负责将上游的视频帧输出到显示设备（HDMI 等）。它完成显示模块的初始化、视频层和通道的配置，并在处理阶段将帧送入 VO 硬件显示。它不能工作在桌面模式下，需要切换到multi-user.target下才能正常工作；而与之对应的openvoElement则必须工作在桌面模式下，即graphical.target。

数据路径与关键阶段：
- 输入数据：
  - `FRAME_META`: 从 `CFrameMeta.images[]` 取 `VIDEO_FRAME_INFO_S`；
  - `BATCH_META`: 如存在 `batchMeta->videoGrid`，优先显示其合成画面 `gridPic`（与 `videoGridElement` 对接）。
- Init（初始化）
  1) 解析 YAML 配置，填充 `VideosinkInitParams`：芯片/接口/时序、输出分辨率、显示区域、各显示通道矩形、像素格式等；
  2) 初始化 VPS 与 VO 模块：`ES_VPS_Init()`、`ES_VO_Init()`；
  3) 初始化 HDMI：`ES_HDMI_Init()` 并读取支持的显示模式 `ES_HDMI_GetDispMode()`；
  4) 若配置 `interface-auto=true`，从 HDMI 支持模式中选择最大分辨率和刷新率，回填 `interface-sync`/`image-size`/`disp-rect`；随后 `ES_HDMI_DeInit()`。
- Start/Enable（在 `Init()` 内完成启用）
  1) 设置 VO 公共属性：`ES_VO_SetPubAttr(die, &pubAttr)`（接口类型与时序）；
  2) 使能 VO 设备：`ES_VO_Enable(die, die)`；
  3) 配置视频层：`ES_VO_SetVideoLayerAttr(die, &layerAttr)`（像素格式、显示区域、图像尺寸）；
  4) 使能视频层：`ES_VO_EnableVideoLayer(die)`；
  5) 为每个通道设置矩形并使能：`ES_VO_SetChnAttr(die, ch, &chnAttr)` + `ES_VO_EnableChn(die, ch)`。
- ProcessData（每帧处理）
  - 选择需要显示的帧指针 `pFrameInfo`（支持 `FRAME_META` 或 `BATCH_META.videoGrid`）；
  - 调用 `ES_VO_SendFrame(die, ch, pFrameInfo, timeout)` 将帧送显；
  - 释放输入 meta 的引用计数，更新性能统计。
- Finish（反初始化）
  - 遍历禁用通道：`ES_VO_DisableChn` → 禁用视频层：`ES_VO_DisableVideoLayer` → 禁用 VO：`ES_VO_Disable`；
  - 释放性能统计与内部资源。

线程/性能：
- 元素内部包含性能统计器 `PerformanceStatic`，打印发送耗时；
- 支持 `dumpflag` 调试（可扩展落盘）。

---

## 2. 真实 ES_ SDK API 列表（按模块分类）
仅列出真实 ES_ API。当前实现中未使用 `PL_ES_` 包装；若未来出现 `PL_ES_` 封装，请在 `pipeline/src/core` 下溯源到对应的 `ES_` API。

1) VPS/显示前置
- `ES_VPS_Init()`：图像处理（VPS）初始化，用于部分平台的图像前处理依赖。

2) VO（Video Output 显示）
- 设备与公共属性：
  - `ES_VO_Init()` / `ES_VO_Disable(die)`
  - `ES_VO_SetPubAttr(die, VO_PUB_ATTR_S*)`：设置接口类型（HDMI/BT 等）与时序（`VO_INTF_SYNC_E`）。
  - `ES_VO_Enable(die, devId)`：使能 VO 设备。
- 视频层：
  - `ES_VO_SetVideoLayerAttr(die, VO_VIDEO_LAYER_ATTR_S*)`：像素格式、显示区域、图像尺寸。
  - `ES_VO_EnableVideoLayer(die)` / `ES_VO_DisableVideoLayer(die)`。
- 通道：
  - `ES_VO_SetChnAttr(die, ch, VO_CHN_ATTR_S*)`：通道矩形布局与优先级；
  - `ES_VO_EnableChn(die, ch)` / `ES_VO_DisableChn(die, ch)`；
  - `ES_VO_SendFrame(die, ch, VIDEO_FRAME_INFO_S*, timeout)`：送帧到指定通道显示。

3) HDMI
- `ES_HDMI_Init()` / `ES_HDMI_DeInit()`；
- `ES_HDMI_GetDispMode(ES_HDMI_ID_0, ES_HDMI_DISPLAY_MODE_S*)`：读取显示器支持模式列表，用于自动选择合适的输出。

4) 常用结构/枚举
- `VO_PUB_ATTR_S`, `VO_VIDEO_LAYER_ATTR_S`, `VO_CHN_ATTR_S`, `VO_INTF_SYNC_E`, `VO_INTF_E`
- `VIDEO_FRAME_INFO_S`, `PIXEL_FORMAT_E`

---

## 3. 配置说明（YAML）
解析逻辑见 `videosinkElement.cpp::parseConfigFile()`：
- 基础参数
  - `die-id`: int，芯片/设备索引；
  - `interface-auto`: bool，是否自动根据 HDMI 支持模式选择输出（优先最大分辨率与刷新率），并回填 `image-size`/`disp-rect`；
  - `interface-sync`: int，显示时序（枚举 `VO_INTF_SYNC_E`，如 720P/1080P/4K/自定义）；
  - `interface-type`: int，接口类型（如 `VO_INTF_HDMI` 等）；
  - `out-fps`: int，目标输出帧率（用于节流/统计，当前实现未强制睡眠）。
- 画布与像素格式
  - `image-size`: [w, h]，视频层图像尺寸；
  - `disp-rect`: [x, y, w, h]，视频层显示区域；
  - `layer-format`: string，视频层像素格式，支持：`nv12`、`rgb`、`bgra`（映射到 `PIXEL_FORMAT_NV12/R8G8B8/B8G8R8A8`）。
- 通道布局
  - `channel-params`: 数组，元素为 `[chnId, x, y, w, h]`，定义每个显示通道的窗口位置与大小；
- 调试
  - `dumpflag`: int/bool，调试开关（可结合扩展实现帧落盘）。

注意：
- 若 `interface-auto=true`，元素会调用 HDMI 模式探测并自动设置 `interface-sync`、`image-size` 与 `disp-rect` 覆盖值；
- 建议确保上游输出分辨率与本元素视频层 `image-size`/`disp-rect` 匹配，避免缩放带来额外负担或显示异常；
- 与 `videoGridElement` 对接时，`videoGrid` 的输出尺寸应与 `videosink` 的视频层配置一致。

---

## 4. 使用方法一：编码方式构建 Pipeline（参考 pl_launch 风格）
示例：将 `videoGridElement` 的网格合成输出直接显示到屏幕。

```cpp
#include "pipeline.h"
#include "elements/videoGridElement/videoGridElement.h"
#include "elements/videoSinkElement/videosinkElement.h"

CPipeline pipe;
CElement* grid = new VideoGridElement("EsVideoGrid0", "src/elements/videoGridElement/videoGridElement_config.yml", 0, 0);
CElement* sink = new VideosinkElement("VideoSink0", "src/elements/videoSinkElement/videosinkElement_config.yml", 0);

// 省略：创建若干上游元素（v4l2/vdec/osd…）并 Link 到 grid
pipe.AddToPipeline(grid, nullptr);
pipe.AddToPipeline(sink, nullptr);
pipe.LinkMany(grid, sink, nullptr);

pipe.Init();
pipe.Start();
pipe.Wait();
pipe.Finish();
```
要点：
- `VideosinkElement` 构造签名 `(name, configPath, dieIndex)`；
- 若 `batchMeta->videoGrid` 非空，`VideosinkElement` 将优先显示 `gridPic`；否则显示 `FrameMeta.images[i]`；
- 视频层 `image-size/disp-rect` 与上游输出分辨率应匹配（可启用 `interface-auto` 由 HDMI 自动协商）。

---

## 5. 使用方法二：通过 `espl_launch` 与 case 脚本
`pl_launch.cpp` 已集成本元素的工厂：
- 元素名：`VideoSink`
- 动态库：`libes_plvideosink.so`
- 工厂函数：`createEsVideoSinkElement(const char* name, const char* path, int dieIndex)`

命令示例：
```bash
./espl_launch \
  -e EsVideoGrid -c elements/videoGridElement/videoGridElement_config.yml \
  -e VideoSink   -c elements/videoSinkElement/videosinkElement_config.yml
```
说明：
- `-e/-c` 依序声明元素与其配置文件；
- 若与解码器/相机直连显示，可将 `VideoSink` 直接接在 `vdecElement`/`v4l2SrcElement` 后；
- Windows 上若运行 `espl_launch.exe`，参数保持一致。

---

## 6. 二次开发与扩展建议
1) 多窗口/拼接布局
- 通过 `channel-params` 定义多通道窗口；可在运行时根据业务动态调整 `VO_CHN_ATTR_S` 达到画中画/分屏等效果；
- 若需自动网格，可在上游使用 `videoGridElement` 合成后接入。

2) 像素格式与色彩空间
- `layer-format` 支持 NV12/RGB/BGRA；若上游像素格式不同，需在上游转换或在本元素层引入 VPS 颜色转换；
- 注意显存对齐与带宽，尽量与上游保持一致以减少拷贝。

3) 同步与帧率控制
- 当前 `out-fps` 仅作为参考/统计；如需严格帧率输出，可在 `ProcessData` 内节流或基于 PTS 与 vsync 做同步；
- 对低延迟应用，合理设置 `ES_VO_SendFrame` 的 `timeout` 并减少队列缓存。

4) HDMI/显示器自适应
- `interface-auto` 通过 HDMI 模式枚举选择最佳输出；也可固定 `interface-sync` 强制分辨率与刷新率；
- 若需多显示器或自定义时序，扩展对多 `VO_INTF_E` 的支持与自定义 `VO_OUTPUT_USER` 模式。

5) 错误处理与调试
- 对 `ES_VO_*/ES_HDMI_*` 调用检查返回码并在失败时回退/重试；
- 开启 `dumpflag` 后可扩展帧落盘以核对实际显示内容；
- 结合性能统计定位瓶颈（送帧耗时、队列等待等）。

6) 与上游模块的接口
- 若直接显示原始 `FrameMeta`，确保其 `VIDEO_FRAME_INFO_S` 的尺寸/步长与视频层属性相符；
- 与 `videoGridElement` 对接时，确保 `gridPic` 的像素格式与视频层像素格式一致，或在上游做格式统一。

---

## 7. 关联参考
- `src/elements/videoSinkElement/videosinkElement_config.yml`：默认配置示例，包含接口、时序、层与通道参数；
- `src/elements/videoGridElement/videoGridElement_readme.md`：网格合成输出与 `VideoSink` 对接说明；
- `pipeline/src/pl_launch/pl_launch.cpp`：编码方式主程序入口与元素工厂注册；
- `pipeline/case/*`：各类 case 脚本，可替换/添加 `VideoSink` 元素快速验证显示链路。

如需我基于你当前显示器 EDID 自动生成一份最优 `videosinkElement_config.yml`，请告知当前常用分辨率/刷新率或直接让我读取运行日志中过的 HDMI 模式列表。
