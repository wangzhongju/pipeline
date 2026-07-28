# vencElement_readme

本文档面向使用者与二次开发者，帮助你：
- 快速理解 `vencElement` 的职责与核心逻辑；
- 熟悉本元素涉及的真实 ES_ SDK API 调用；
- 提供两种使用方式：编码方式构建 Pipeline 与通过 `espl_launch` 构建；
- 指导如何基于源码定制和扩展视频编码插件。

- 源码目录：`src/elements/vencElement/`
  - 主要文件：`vencElement.h`, `vencElement.cpp`
  - 通用/封装：`common/pl_comm_venc.c`, `common/pl_encwrapper.[ch]`, `common/pl_option_enc.[ch]`
  - 配置样例：`vencElement_config.yml`, `vencoder.yaml`

---

## 1. 功能与核心逻辑
`vencElement` 是视频编码器元素：
- 上游输入：未压缩视频帧 `VIDEO_FRAME_INFO_S`（通常来自 `vdecElement`、`v4l2SrcElement`、或预处理/OSD 后的图像）。
- 处理：调用 ES_VENC 硬件编码 H.264/H.265/JPEG；支持多种 GOP/码率模式/ROI/QPMap 等参数；可获取编码帧并可选落盘。
- 下游输出：编码码流包（可发往 `muxElement`、`filesink`、网络传输等）。

典型执行路径（简化）：
1) Init
   - 解析 YAML（见“配置说明”）。
   - 组合 `TEST_Client_S_ENC` 与 `TEST_CHN_S_ENC`（组/通道参数），记录 `type/profile/gop/rc/pixfmt/bitrate` 等；
   - 创建性能统计对象 `sendFramePerformance/getStreamPerformance`。

2) Start
   - 首次模块级初始化与参数设置：`ES_VENC_Init`、`ES_VENC_GetModParam`/`ES_VENC_SetModParam`；
   - 为本元素通道计算并创建编码通道：`ES_VENC_CreateChn(grpId, 0, &attr)`；
   - 设置通道参数：`ES_VENC_SetChnParam(grpId, &chnParam)`；
   - 可选：读取/设置 H.26x VUI/Trans/Entropy/DBLK/SAO/RC 等高级参数。
   - 若启用码流落盘：准备输出文件（后续在取流线程写入）。

3) ProcessData（送帧）
   - 从上游 `CFrameMeta` 取出 `VIDEO_FRAME_INFO_S`；
   - 调用 `ES_VENC_SendFrame(chnId, videoFrameInfo, -1)` 将帧送入编码器（阻塞直到可送）。

4) 取流/回收（内部线程或轮询）
   - 可通过 `ES_VENC_GetFd` 结合 `select/poll` 触发，或直接循环 `ES_VENC_QueryStatus`；
   - `ES_VENC_GetStream(chnId, stream, -1)` 取出码流包；
   - 传递到下游或 `filesink`；
   - 用完后必须 `ES_VENC_ReleaseStream(chnId, stream)` 归还；
   - JPEG 模式下可能逐帧单独保存。

5) Wait/Finish
   - `ES_VENC_StopRecvFrame(chnId)`/`ES_VENC_StopRecvPic`（视接口命名与实现）；
   - 关闭编码 fd：`ES_VENC_CloseFd(chnId)`；
   - 通道销毁与模块反初始化：`ES_VENC_Deinit()`；
   - 关闭文件与释放性能统计对象。

---

## 2. 真实 ES_ SDK API 使用（按功能分类）
以下为在本元素与其通用封装中实际调用到的 ES_ 接口（通过 `pl_comm_venc.c`/`pl_encwrapper.c` 已溯源）。不同 SoC/SDK 版本可能略有差异，以你工程的头文件为准。

A) 模块生命周期与全局参数（`es_venc.h`）
- `ES_VENC_Init()` / `ES_VENC_Deinit()`
- `ES_VENC_GetModParam(VENC_PARAM_MOD_S*)` / `ES_VENC_SetModParam(const VENC_PARAM_MOD_S*)`

B) 通道创建与基础参数
- `ES_VENC_CreateChn(VENC_CHN grpId, ES_S32 subChn, const VENC_CHN_ATTR_S* attr)`
- `ES_VENC_GetChnAttr(VENC_CHN grpId, VENC_CHN_ATTR_S* attr)`
- `ES_VENC_SetChnParam(VENC_CHN grpId, const VENC_CHN_PARAM_S* chnParam)`
- `ES_VENC_GetChnParam(VENC_CHN grpId, VENC_CHN_PARAM_S* chnParam)`

C) H.264/H.265 协议相关高级参数（按需）
- Intra 刷新：`ES_VENC_GetIntraRefresh` / `ES_VENC_SetIntraRefresh`
- 码率策略：`ES_VENC_GetRcParam` / `ES_VENC_SetRcParam`（含 `H264CBR/VBR/AVBR/QVBR/CVBR/QPMAP` 与 H.265 对应变种）
- H.264：`ES_VENC_GetH264VUI`/`ES_VENC_SetH264VUI`, `ES_VENC_GetH264Trans`/`ES_VENC_SetH264Trans`,
  `ES_VENC_GetH264Entropy`/`ES_VENC_SetH264Entropy`, `ES_VENC_GetH264Dblk`/`ES_VENC_SetH264Dblk`
- H.265：`ES_VENC_GetH265VUI`/`ES_VENC_SetH265VUI`, `ES_VENC_GetH265Trans`/`ES_VENC_SetH265Trans`,
  `ES_VENC_GetH265SAO`/`ES_VENC_SetH265SAO`, `ES_VENC_GetH265PredUnit`/`ES_VENC_SetH265PredUnit`
- ROI/SSE/QPMap 等在封装中按 rcMode/文件输入选择开启（见 `pl_option_enc.*`）。

D) 运行期送帧/取流控制
- 送帧：`ES_VENC_SendFrame(VENC_CHN, const VIDEO_FRAME_INFO_S*, ES_S32 milliSec)`
- 取流：`ES_VENC_GetStream(VENC_CHN, VENC_STREAM_S*, ES_S32 milliSec)`
- 归还：`ES_VENC_ReleaseStream(VENC_CHN, const VENC_STREAM_S*)`
- 查询状态：`ES_VENC_QueryStatus(VENC_CHN, VENC_CHN_STATUS_S*)`
- 事件/FD：`ES_VENC_GetFd(VENC_CHN)` / `ES_VENC_CloseFd(VENC_CHN)`
- 停止接收：`ES_VENC_StopRecvFrame(VENC_CHN)`（或 `ES_VENC_StopRecvPic`，按 SDK 实际）

注：上述均为真实 ES_ 前缀 API；代码中的 `PL_` 前缀仅做参数组装与通用流程封装，最终调用落在 ES_ 接口。

---

## 3. 配置文件说明（YAML）
示例：`vencElement_config.yml`（字段与 `vencElement.cpp::parseYamlCfg` 一致）
```yaml
encoder:
  format_out: 1        # 输出码流：0=H265,1=H264,2=JPEG
  profile: 100         # 编码 profile（示例值），随 SoC/SDK 定义
  gop_mode: 0          # GOP 模式：0 NORMALP | 1 DUALREF | 2 SMARTREF | 3 ADVSMARTREF | 4 BIPREDB | 5 LOWDELAYB
  gop_para:            # 各模式下的细化参数（按需使用）
    BFrmNum: 0
    SPInterval: 0
    SPQpDelta: 0
    IPQpDelta: 0
    BgInterval: 0
    BgQpDelta: 0
    ViQpDelta: 0
    BQpDelta: 0
  rc:                  # 码率控制
    rc_mode: 1         # H264CBR/VBR/AVBR/QVBR/CVBR/FIXQP/QPMAP 等及 H265 对应
    frameRate: 25
  gop: 50              # GOP 大小
  jpegQFactor: 80      # JPEG 质量（仅 JPEG/MJPEG 有效）
  pixelFormat: nv12    # 输入像素格式（将映射到 PIXEL_FORMAT_E）
  bitrate: 4000000     # 目标码率 bps
  dumpframe: 0         # 是否启用编码帧转储
filesink:
  filename: "D:/output/enc_output.h264"
  dumppack: 0
```
要点：
- `format_out` 与 `type`（`PAYLOAD_TYPE_E`）绑定，影响后续通道属性与 RC 结构体的选择；
- `gop_mode/gop/gop_para` 与 `rc.rc_mode` 共同决定帧间结构与码率行为；
- 输入 `pixelFormat` 与上游输出必须匹配（例如 NV12 对 NV12）。

---

## 4. 两种使用方式

### 4.1 编码方式构建 Pipeline（参考 `src/pl_launch/pl_launch.cpp`）
```cpp
#include "element.h"
#include "vencElement.h"

CElement* src   = createEsV4l2SrcElement("EsV4L2",   ".../v4l2SrcElement_config.yml", dieId);
CElement* osd   = createEsOsdElement   ("EsOsd",    ".../osdElement_config.yml", dieId); // 可选
CElement* venc  = createEsVencElement  ("EsVenc",   ".../vencElement_config.yml", dieId);
CElement* sink  = createEsFileSinkElement("EsSink",".../fileSinkElement_config.yml", dieId); // 可选

LinkMany(src, osd);
LinkMany(osd, venc);
LinkMany(venc, sink);

APP_CHECK(src->Init());
APP_CHECK(osd->Init());
APP_CHECK(venc->Init());
APP_CHECK(sink->Init());

APP_CHECK(src->Start());
APP_CHECK(osd->Start());
APP_CHECK(venc->Start());
APP_CHECK(sink->Start());

APP_CHECK(src->Wait());
APP_CHECK(osd->Wait());
APP_CHECK(venc->Wait());
APP_CHECK(sink->Wait());

APP_CHECK(src->Finish());
APP_CHECK(osd->Finish());
APP_CHECK(venc->Finish());
APP_CHECK(sink->Finish());
```
说明：
- 上述仅示例链路；你的实际工程可能是 `vdec -> preProcess -> venc -> mux/file`。
- `ProcessData()` 内部会将 `VIDEO_FRAME_INFO_S` 送入 `ES_VENC_SendFrame`；取流线程通过 `ES_VENC_GetStream` 拉取并向下游传递（或落盘）。

### 4.2 通过 `espl_launch` 构建（参考 `pipeline/case/*.sh`）
```bash
# 例：v4l2(nv12) -> venc(h264) -> file
espl_launch \
  -e EsV4L2 -c pipeline/src/elements/v4l2SrcElement/v4l2SrcElement_config.yml \
  -e EsVenc -c pipeline/src/elements/vencElement/vencElement_config.yml \
  -e EsSink -c pipeline/src/elements/fileSinkElement/fileSinkElement_config.yml
```
说明：
- `-e` 指定元素工厂名（与 `createEsVencElement` 等一致），`-c` 指定 YAML 配置；
- 按需插入 demux/OSD/mux 等其他元素；查看 `pipeline/case` 中已有脚本作为模板。

---

## 5. 定制与扩展建议
1) 通道属性与格式支持
- 若需新增像素格式或特殊对齐/步长，需在通道属性计算处补齐 `stride/size/PIXEL_FORMAT_E` 映射，并确保与上游一致；
- JPEG/MJPEG 单帧编码的文件输出路径在 `saveStreamJpeg` 可调整为按 PTS/序号命名。

2) GOP/RC 策略
- `pl_option_enc.*` 已提供 ROI/SSE/QPMap 的参数入口，可通过外部文件或 YAML 控制；
- 针对低时延/高质量需求，分别调整 `gop_mode/gop/rc.rc_mode/bitrate/profile`，并在 `ES_VENC_SetRcParam` 细化参数。

3) 线程与事件模型
- 取流可用 `ES_VENC_GetFd` + `select`/`poll` 降低忙等；阻塞超时策略通过 `GetStream(..., milliSec)` 控制；
- 若多路编码，考虑按芯片 `die-id`/NUMA 绑定线程亲和性，避免跨片内存访问。

4) 性能与内存
- 编码侧零拷贝依赖上游共享的物理/IOVA fd；若需拷贝，注意缓存池大小与对齐；
- 使用内置 `PerformanceStatic` 对 `SendFrame/GetStream/ReleaseStream` 打点，辅助瓶颈定位。

5) 可靠性与排障
- 取流失败（返回码非 ES_SUCCESS）时检查 `ES_VENC_QueryStatus` 的剩余帧与状态；
- 保证每次 `GetStream` 都有成对的 `ReleaseStream`，否则会导致内部缓冲耗尽；
- 结束时按顺序停止接收、关闭 fd、反初始化，避免句柄泄漏。

---

## 6. 参考代码位置
- 元素核心：`vencElement.h`, `vencElement.cpp`
- 编码封装/通用：`common/pl_comm_venc.c`, `common/pl_encwrapper.[ch]`, `common/pl_option_enc.[ch]`
- 关键 ES_ 头：`es_venc.h`, `es_comm_video.h`

如需补充特定 SoC 下的最佳参数模板、码控建议、或端到端样例，请告知你的输入分辨率/帧率/目标码率与延迟需求，我可进一步完善。
