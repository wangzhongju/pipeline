# videoGridElement_readme

本文档面向使用者与二次开发者，帮助你：
- 快速理解 `videoGridElement` 的职责与核心逻辑；
- 熟悉本元素涉及的真实 ES_ SDK API 调用（不展示 `PL_ES_`，并溯源到真实 `ES_` API）；
- 提供两种使用方式：编码方式构建 Pipeline 与通过 `espl_launch` 构建；
- 指导如何基于源码定制和扩展视频九宫格/多宫格合成插件。

- 源码目录：`src/elements/videoGridElement/`
  - 主要文件：`videoGridElement.h`, `videoGridElement.cpp`
  - 配置解析：`yaml_parser.h`, `yaml_parser.cpp`
  - 配置样例：`videoGridElement_config.yml`, `videogrid.yaml`

---

## 1. 功能与核心逻辑
`videoGridElement` 是一个将多路输入视频帧合成为一张网格大图（Grid）的图像处理元素，典型用于多路预览/墙面拼接：
- 上游输入：批次帧 `CBatchMeta`，其中每路在 `CFrameMeta` 的 `images[...]` 中包含 `VIDEO_FRAME_S`（通常来自 `v4l2SrcElement`/`vdecElement`/`osdElement` 等）。
- 处理：根据配置的 `rows/columns`、输出画布 `width/height`，计算每格目标区域，将各路源帧按 `RECT` 复制到目标画布；在 RISC-V 平台通过 VPS 硬件加速 `ES_VPS_MultiSourcesBlit` 完成多源拷贝/缩放/贴合；支持可选网格线/特殊高亮区域填充。
- 下游输出：将合成后的画布以 `CVideoGridMeta` 形式挂到 `batchMeta->videoGrid`，供 `videosinkElement` 显示或后续模块消费。

核心执行路径（简化）：
1) Init
   - 解析 YAML（见“配置说明”）；创建性能统计器与 `MetaPool<CVideoGridMeta>`；
   - 初始化 VPS 引擎：`ES_VPS_Init()`；
   - 初始化内部状态（颜色、特殊区域、起始 padIndex 等）。

2) Start
   - 创建 VB 内存池（通过封装实现预取若干块，详见“真实 ES_ API 列表”）：
     - 为输出画布分配池与备份帧（用于双缓冲/拷贝回退）。
     - 读取并加载 `text-pic`（用于底色/水印）的原始 NV12 数据到专用 VB 块（可选）。

3) ProcessData
   - 从 `CBatchMeta` 遍历 N 路输入，按 `padIndex - startPadIndex` 计算其在网格中的目标格；
   - 组织输入 `VIDEO_FRAME_S frameIn[]`、源 `rectSrc[]` 与目标 `rectDst[]`；
   - 构造输出画布 `frameOut`，设置其 `fd/stride/offset/pixelFormat`；
   - 在 RISC-V 平台调用 `ES_VPS_MultiSourcesBlit(frameIn, imgNum, rectSrc, NULL, rectDst, &frameOut, HW_TYPE_HAE)` 实施多源拷贝/缩放至目标画布；
   - 将输出的画布属性填充至 `CVideoGridMeta` 并挂载到 `batchMeta->videoGrid`；
   - 若启用 dump，则 `ES_SYS_Mmap/ES_SYS_Munmap` 映射后落盘原始数据。

4) Finish
   - 销毁性能统计、`MetaPool`；
   - 销毁 VB 池；
   - 反初始化 VPS：`ES_VPS_Deinit()`。

时序与线程：
- 本元素为同步型（`mPerfType = SYNC_PERF_ELEMENT`），在管线线程中执行；
- 可结合 `set_thread_affinity/set_thread_priority` 绑定核与优先级（已在实现中调用）。

---

## 2. 本元素使用到的真实 ES_ SDK API（按模块分类）
仅列出真实 ES_ API；对 `PL_ES_` 封装已溯源到核心 ES_ 调用。

1) VPS（图像处理/拼接）
- `ES_VPS_Init()` / `ES_VPS_Deinit()`：VPS 模块初始化/反初始化。
- `ES_VPS_MultiSourcesBlit(const VIDEO_FRAME_S* src, int srcNum, const RECT_S* srcRect, const RECT_S* clipRect, const RECT_S* dstRect, VIDEO_FRAME_S* dst, HWTYPE_E hw)`：多源图像拷贝/缩放/贴合。
- `ES_VPS_Fill(VIDEO_FRAME_S* frame, const ES_U32* color, int colorNum, const RECT_S* rects, int rectNum, HWTYPE_E hw)`：矩形区域填充颜色（用于网格线/高亮等）。
- （可选/注释）`ES_VPS_SetProperty(VPS_PROPERTY_E, ES_VOID*)`：设置引擎属性（例如 `HW_TYPE_HAE`）。

2) VB（视频缓冲/内存池）
- 通过封装 `PL_ES_VB_*` 实现预取与句柄管理，但其内部使用的真实 ES_ API 为：
  - `ES_VB_CreatePool(const VB_POOL_CONFIG_S*, VB_POOL*)`：创建缓冲池。
  - `ES_VB_GetBlock(VB_POOL, ES_U64 blkSize, const ES_CHAR* zone, ES_U64* memFd)`：从池中获取块。
  - `ES_VB_AllocIOVA(ES_U64 memFd, VB_UID_E uid, ES_VOID** pIOVA)`：为块分配 IOVA（硬件访问地址）。
  - `ES_VB_DestroyPool(VB_POOL)`：销毁池。

3) SYS（系统内存映射）
- `ES_SYS_Mmap(ES_U64 memFd, ES_U64 size, SYS_CACHE_MODE_E cacheMode)`：将 VB 块映射至 CPU 虚拟地址。
- `ES_SYS_Munmap(ES_VOID* vaddr, ES_U64 size)`：解除映射。

4) 结构/类型
- `VIDEO_FRAME_S`, `VIDEO_FRAME_INFO_S`, `RECT_S`, `PIXEL_FORMAT_E` 等。

说明：
- 代码中出现的 `PL_ES_VB_CreatePool/PL_ES_VB_GetBlock/PL_ES_VB_DestroyPool/PL_ES_VB_ReleaseBlock` 位于 `src/core/src/pl_mem_wrap.cpp`，内部真实调用 `ES_VB_CreatePool/ES_VB_GetBlock/ES_VB_AllocIOVA/ES_VB_DestroyPool` 并做了块队列管理，本文不将 `PL_ES_` 计入 ES_ API 列表。

---

## 3. 配置说明（YAML）
来自 `yaml_parser.cpp/.h`，关键字段如下（如未配置使用默认值，单位见注释）：
- 基础
  - `die-id`: int，芯片 DIE/核选择。
  - `poolsize`: int，输出画布池中块数量（合成缓冲数量）。
  - `rows`: int，网格行数；`columns`: int，网格列数。
  - `width`: int，输出画布宽；`height`: int，输出画布高。
  - `stride-align`: int，步长对齐（用于计算 NV12 步长与帧大小）。
  - `pixelFormat`: enum，输出像素格式，默认 NV12（代码中按 NV12 计算 stride/size）。
  - `dumpFlag`: bool，是否原始数据落盘（调试）。
- 文字/底图（可选）
  - `text-pic-path`: string，NV12 原始图路径；
  - `text-pic-width`: int，宽；`text-pic-height`: int，高；三者必须均有效（代码有断言）。
- 颜色与特殊区域（可选）
  - `color[3]`: int, RGB 颜色（用于填充/网格线）。
  - `specialrectnum`: int，特殊高亮矩形数量；`specialrow[]/specialcol[]` 指定其网格坐标。
- 其他
  - `channelID`: int，从 `CFrameMeta.images[channelID]` 选择用于合成的通道（默认 0）。

内部派生参数
- `stride[3]`, `sizePerFrame`：由 `width/height/pixelFormat/stride-align` 推导得到，用于 VB 分配与帧布局。

---

## 4. 使用方法一：编码方式构建 Pipeline（参考 pl_launch 风格）
示例：四路输入合成 2x2 网格后送往 `videosinkElement` 显示。

```cpp
#include "pipeline.h"
#include "elements/videoGridElement/videoGridElement.h"

// 省略：创建 v4l2/vdec 等上游元素，命名为 src0..src3，创建 videosink

CPipeline pipe;
CElement* grid = new VideoGridElement("EsVideoGrid0", "src/elements/videoGridElement/videoGridElement_config.yml", /*die*/0, /*startPadIndex*/0);

pipe.AddToPipeline(src0, nullptr);
pipe.AddToPipeline(src1, nullptr);
pipe.AddToPipeline(src2, nullptr);
pipe.AddToPipeline(src3, nullptr);
pipe.AddToPipeline(grid, nullptr);
pipe.AddToPipeline(videosink, nullptr);

pipe.LinkMany(src0, grid, nullptr);
pipe.LinkMany(src1, grid, nullptr);
pipe.LinkMany(src2, grid, nullptr);
pipe.LinkMany(src3, grid, nullptr);
pipe.LinkMany(grid, videosink, nullptr);

pipe.Init();
pipe.Start();
pipe.Wait();
pipe.Finish();
```

要点：
- `VideoGridElement` 构造签名为 `(name, configPath, dieIndex, startPadIndex)`；
- 上游每路 `CFrameMeta.padIndex` 将决定其映射到网格中的位置（结合 `startPadIndex`）；
- `videosinkElement` 会优先显示 `batchMeta->videoGrid->gridPic`（已在源码中处理）。

---

## 5. 使用方法二：通过 `espl_launch` 构建（参考 pipeline/case）
`pl_launch.cpp` 已内置加载：
- 元素名：`EsVideoGrid`
- 动态库：`libes_plvideogrid.so`
- 工厂函数：`createEsVideoGridElement(const char* name, const char* path, int dieIndex, int startPadIndex)`
- 额外参数：`-startPadIndex <N>`

命令示例：
```bash
./espl_launch \
  -e V4L2Src -c elements/v4l2SrcElement/v4l2src_config.yml \
  -e EsVideoGrid -c elements/videoGridElement/videoGridElement_config.yml -startPadIndex 0 \
  -e VideoSink -c elements/videosinkElement/videosinkElement_config.yml
```
说明：
- `-e/-c` 用于顺序声明元素与其配置；
- 需要确保 `videosinkElement` 画布尺寸与 `videoGridElement` 合成输出一致（源码注释已有特别提示）；
- 也可将 `EsVideoGrid` 接到编码器/复用器以输出码流文件或网络推流。

---

## 6. 二次开发与扩展建议
1) 画布与格式
- 如需支持更多像素格式（如 BGRA/RGB），在 `videoGridMeta2VoInfo` 与输出 `frameOut` 处补充 stride/offset 计算；
- 统一以 `stride-align` 驱动步长计算，避免 cache line/带宽浪费。

2) 网格布局与动态排布
- 现使用 `rows/columns` 静态网格；可引入运行时动态布局（按活跃路数/优先级调整 `rectDst[]`）；
- `startPadIndex` 用于偏移起始位置，适合环形/滚动布局，可开放更多 CLI 参数。

3) 性能优化
- RISC-V 平台已使用 VPS 硬加速；其他平台可落入软件实现或添加条件编译；
- 合理设置 VB `poolsize`，避免频繁分配；必要时增加双缓冲减少覆盖等待；
- 使用 `ES_VB_AllocIOVA` 并绑定硬件引擎可减少拷贝。

4) 可靠性与调试
- 确保每次映射后 `ES_SYS_Munmap` 对应释放；
- 开启 `dumpFlag` 复核输出画布是否与期望一致；
- 打印 `imgNum/batchIndex/padIndex` 协助排查路数缺失或映射错误。

5) 与下游对接
- `videosinkElement` 读取 `batchMeta->videoGrid->gridPic` 直接显示；若改为输出 `CImage` 给编码器，需要在本元素中构造 `VIDEO_FRAME_INFO_S` 并传递至下游；
- 若要叠加 OSD/文本，可在合成后再调用 `ES_VPS_Fill` 或接入 `osdElement`。

6) 内存池封装替换
- 目前采用 `PL_ES_VB_*` 对 ES_VB 做预取与句柄管理；若希望更透明地使用原生 ES_VB，可直接以 `ES_VB_CreatePool/ES_VB_GetBlock/ES_VB_DestroyPool` 重写对应路径，但需自行管理块生命周期与并发。

---

## 7. 关联示例与注意事项
- `src/README.md` 表中已有 `EsVideoGrid` 的 CLI 参数说明（`-startPadIndex`）。
- `videosinkElement_config.yml` 注释强调了画布尺寸需与 `videoGrid` 合成输出一致。
- 在 `yaml_parser.cpp` 中对 `text-pic-*` 有断言，请确保提供正确的 NV12 原始图与尺寸。

如需补充更多 case 脚本（如 4 路 V4L2 → Grid → Sink 或解码 → Grid → 编码 → 复用），我可以基于现有 `pipeline/case` 模板生成。
