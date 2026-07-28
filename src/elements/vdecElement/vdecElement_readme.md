# vdecElement_readme

本说明面向使用与二次开发 vdecElement 的用户与开发者。内容覆盖：核心代码逻辑、真实 ES_ SDK API 列表、两种使用方式（编码与 espl_launch）、以及基于源码进行定制扩展的建议。

- 元素源码目录：`src/elements/vdecElement/`
  - 主要文件：`vdecElement.h`, `vdecElement.cpp`, `common/pl_comm_video.c`, `common/pl_comm_dec.h`, `common/pl_option_dec.c`

---

## 1. 核心代码逻辑概览

vdecElement 是视频解码 Source/Transform 元素：
- 上游接收压缩码流包（`CVideoPacketMeta`，内含 `VDEC_STREAM_S` 指针），
- 通过 ES_VDEC 硬件解码，
- 在独立取帧线程中从 VDEC 组-通道拉取解码帧，封装为 `CFrameMeta` + `CImageVd`，
- 下发给下游（如预处理、推理、OSD 或 Sink）。

关键路径：
1) Init
- 解析 YAML：`param.align`, `param.die-id`, `param.poolsize`；`output.picture-0/1` 的 `video-format`、`scale`、`crop`；`dump.enable`。
- 组装 `DEC_CHN_S`（通道参数），写入静态全局 `gDecClient`（支持多组，当前代码为每元素 1 组）。
- 根据上游 `VIDEO_STREAM_INFO`（由 `m_PreviousElementVec[0]->InfoQuery` 获取）补全 `type/width/height`，并调用 `validateAndCreateChns_ext` 完成 VDEC 通道参数计算。
- 创建性能统计与对象池：`MetaPool<CFrameMeta>`、`MetaPool<CImageVd>`。

2) Start
- 首次启动模块级资源：
  - `COMM_VDEC_InitVBPool(&gDecClient)`：配置/创建 VB 池（模块池或用户池）。
  - `COMM_VDEC_Start(&gDecClient)`：调用 ES_VDEC 系列接口完成 `Init`/`CreateGrp`/`AttachVbPool`/`EnableChn`/`SetChnMode`/`StartRecvStream`。
- 为本元素所属解码组创建“取帧线程”`plStartGetFrame` 并运行。

3) ProcessData（喂码流）
- 上游传入 `CVideoPacketMeta`，取出 `VDEC_STREAM_S*`：
  - 根据配置设置 `bDisplay`，
  - 循环调用 `ES_VDEC_SendStream(grpId, stream, milliSec)`；若失败且发送线程仍在，重试；
  - 若 `bEndOfStream = ES_TRUE`，调用 `ES_VDEC_StopRecvStream(grpId)`，并标记 EOS。
- 归还上游 `videoPacketMeta` 引用计数。

4) 取帧线程 `plStartGetFrame`
- 绑定 NUMA/CPU 亲和，循环：
  - `ES_VDEC_QueryStatus` 判断是否有帧/是否结束；
  - 对开启的输出通道 `i` 遍历取帧：`ES_VDEC_GetFrame(grpId, i, frame, timeout)`；
  - 成功后：
    - `CFrameMeta` 从池申请，写 `pts/index/padIndex/dieIndex`；
    - `CImageVd` 从池申请，`setparam(grpId, i, frame)`，加入 `fMeta->images`；
    - 可选写 raw dump（按 `dump.enable` 和通道开关）。
  - 将 `fMeta` 下发：`TransMitToNextToProcess`。

5) CImageVd::release
- 若 `misIOVA` 为真，释放 IOVA：`ES_VB_FreeIOVA(VB_UID_HAE, frame.fd)`；
- 调 `ES_VDEC_ReleaseFrame(grpId, vdChn, frame)` 归还帧；
- 回收 `CImageVd` 自身到池。

6) Wait/Finish
- `Wait`：等待发送与取帧线程退出。
- `Finish`：销毁解码组、VDEC 去初始化、释放 VB 池、关闭 dump 文件、释放对象池与性能统计。

---

## 2. 真实 ES_ SDK API 使用清单（分类）

以下仅列真实 ES_ 前缀 API。代码中可能出现的工具/封装（位于 `common/pl_comm_*`）已进一步溯源至 ES_ 调用。

A) VDEC 模块管理与解码流程（`es_vdec.h`）
- 模块生命周期：
  - `ES_VDEC_Init()` / `ES_VDEC_Deinit()`
  - `ES_VDEC_GetModParam(VDEC_MOD_PARAM_S*)` / `ES_VDEC_SetModParam(const VDEC_MOD_PARAM_S*)`
- 组与通道：
  - `ES_VDEC_CreateGrp(VDEC_GRP grpId, ES_S32 dieId, const VDEC_GRP_ATTR_S* attr)`
  - `ES_VDEC_DestroyGrp(VDEC_GRP grpId)`
  - `ES_VDEC_AttachVbPool(VDEC_GRP grpId, const VDEC_GRP_POOL_S* pool)`（当使用用户 VB 池时）
  - `ES_VDEC_GetGrpParam(VDEC_GRP, VDEC_GRP_PARAM_S*)` / `ES_VDEC_SetGrpParam(VDEC_GRP, const VDEC_GRP_PARAM_S*)`
  - `ES_VDEC_EnableChn(VDEC_GRP, VDEC_CHN)`
  - `ES_VDEC_GetChnMode(VDEC_GRP, VDEC_CHN, VDEC_CHN_MODE_S*)` / `ES_VDEC_SetChnMode(VDEC_GRP, VDEC_CHN, const VDEC_CHN_MODE_S*)`
  - `ES_VDEC_StartRecvStream(VDEC_GRP)` / `ES_VDEC_StopRecvStream(VDEC_GRP)`
- 码流收发与帧获取：
  - `ES_VDEC_SendStream(VDEC_GRP, const VDEC_STREAM_S*, ES_S32 milliSec)`
  - `ES_VDEC_GetFrame(VDEC_GRP, VDEC_CHN, VIDEO_FRAME_INFO_S* out, ES_S32 milliSec)`
  - `ES_VDEC_ReleaseFrame(VDEC_GRP, VDEC_CHN, VIDEO_FRAME_INFO_S* frame)`
  - `ES_VDEC_QueryStatus(VDEC_GRP, VDEC_GRP_STATUS_S*)`

B) VB（视频缓冲）管理（`es_vb_memory.h`）
- 模块公共池（按模块汇聚）：
  - `ES_VB_SetModPoolConfig(VB_UID_E uid, const VB_CONFIG_S* cfg)`
  - `ES_VB_ModPoolInit(VB_UID_E uid)` / `ES_VB_ModPoolExit(VB_UID_E uid)`
- 用户独立池（逐组创建）：
  - `ES_VB_CreatePool(const VB_POOL_CONFIG_S* cfg, VB_POOL* poolId)`
  - `ES_VB_DestroyPool(VB_POOL poolId)`
- IOVA 相关：
  - `ES_VB_FreeIOVA(VB_UID_E uid, ES_U64 iova_fd)`

C) 重要结构/类型（来自 ES 公共头）
- `VDEC_GRP_ATTR_S`, `VDEC_GRP_PARAM_S`, `VDEC_CHN_MODE_S`, `VDEC_GRP_STATUS_S`
- `VDEC_GRP_POOL_S`, `VDEC_STREAM_S`
- `VIDEO_FRAME_INFO_S`, `PIXEL_FORMAT_E`, `SCALE_S`, `CROP_INFO_S`

说明：vdecElement 中的 `pl_comm_video.c`/`pl_comm_dec.h` 是便捷封装，真实硬件交互点全部如上列 ES_ API。

---

## 3. 配置文件说明（YAML）

文件示例：`src/elements/vdecElement/vdecElement_config.yml`

```yaml
param:
  die-id: 0           # 芯片/NUMA ID，范围视平台而定
  align: 1            # 帧对齐（宽对齐单位），>0 时生效
  poolsize: 10        # displayFrameNum（显示帧缓存数），影响底层帧缓存深度

output:
  picture-0:          # pp0 主输出
    video-format: nv12  # 可选：nv12 | rgb | bgra | bgr
    scale: [0, 0]       # 可选：缩放后的宽高；[0,0] 表示不缩放
    crop:  [0, 0, 0, 0] # 可选：裁剪矩形 x,y,w,h；全 0 表示不裁剪
  picture-1:          # pp1 次输出（可选，若不需要可省略）
    video-format: nv12  # 可选：nv12 | yuy2（不支持 rgb/rgba）
    scale: [0, 0]
    crop:  [0, 0, 0, 0]

# 调试：原始帧导出
dump:
  enable: false
```

要点：
- `video-format` 将映射到 `PIXEL_FORMAT_E`，不同通道的支持能力不同（pp1 不支持 rgb/rgba）。
- `poolsize` 会映射到 `displayFrameNum`，同时 `frameBufCnt/minBufSize` 等在内部根据输入分辨率/格式计算。
- 具体组/通道参数落地在 `DEC_CHN_S` 并经 `validateAndCreateChns_ext` 及 `COMM_VDEC_Start` 的 ES_ API 生效。

---

## 4. 两种使用方式

### 4.1 方式一：编码方式构建 Pipeline（参考 `src/pl_launch/pl_launch.cpp`）

伪代码示例（仅展示关键点）：

```cpp
#include "element.h"
#include "vdecElement.h"

// 1) 构建元素（解码器通常接在 demux 或网络 source 之后）
CElement* demux  = createEsDemuxElement("EsDemux",   "path/to/demux.yml", dieId);
CElement* vdec   = createEsVdecElement ("EsVdec",    "path/to/vdecElement_config.yml", dieId);
CElement* sink   = createEsTestSinkElement("EsSink", "path/to/sink.yml", dieId);

// 2) 链接：demux(pad) -> vdec -> sink
LinkMany(demux, vdec);
LinkMany(vdec, sink);

// 3) 初始化
APP_CHECK(demux->Init());
APP_CHECK(vdec->Init());
APP_CHECK(sink->Init());

// 4) 启动
APP_CHECK(demux->Start());
APP_CHECK(vdec->Start());
APP_CHECK(sink->Start());

// 5) 等待/结束
APP_CHECK(demux->Wait());
APP_CHECK(vdec->Wait());
APP_CHECK(sink->Wait());

APP_CHECK(demux->Finish());
APP_CHECK(vdec->Finish());
APP_CHECK(sink->Finish());
```

注意：
- vdecElement 会在 `Start()` 时根据全局 `gDecClient` 初次执行 VDEC 模块与 VB 池的初始化与创建解码组，并启动取帧线程。
- 码流包通过上游元素在 `ProcessData()` 中透传为 `VDEC_STREAM_S`，vdecElement 内部以 `ES_VDEC_SendStream` 喂入解码器。
- 当码流 `bEndOfStream` 为真时，vdecElement 调用 `ES_VDEC_StopRecvStream` 并在取帧线程中发送 EOS 元数据向下游收尾。

### 4.2 方式二：通过 espl_launch 构建（参考 `pipeline/case` 脚本）

在你已有的 case 模板基础上添加 VDEC 元素条目（各 case 脚本命名不一，这里给出通用示例）：

```bash
# 示例：demux(h264)->vdec(nv12)->sink
espl_launch \
  -e EsDemux -c pipeline/src/elements/demuxElement/demuxElement_config.yml \
  -e EsVdec  -c pipeline/src/elements/vdecElement/vdecElement_config.yml \
  -e EsSink  -c pipeline/src/elements/testSinkElement/testSinkElement_config.yml
```

说明：
- `-e` 指定元素工厂名，一般与 `createEsXxxElement` 一致（本元素为 `EsVdec`）。
- `-c` 指定对应 YAML 配置文件路径。
- 真实 case 中还会包含输入源、pad 绑定、以及日志/转储参数等，请根据项目中现有 case 模板增减参数。

---

## 5. 二次开发与定制建议

1) 通道与格式扩展
- 当前实现支持 `pp0/pp1` 双输出，pp0 支持 `nv12/rgb/bgr/bg ra`，pp1 支持 `nv12/yuy2`；如需新增格式：
  - 在 `validateAndCreateChns_ext`/`getChnMode` 路径处理 `pixelFormat`、`stride/size` 计算与 VB 池大小，
  - 保证 `PL_GetPicBufferSize` 计算与 `VDEC_GRP_ATTR_S.frameBufSize` 匹配。

2) VB 策略与内存拓扑
- 两种模式：模块公共池（`VB_SOURCE_MODULE`）与用户自建池（`VB_SOURCE_USER`）。
  - 公共池需 `ES_VB_SetModPoolConfig` + `ES_VB_ModPoolInit`；
  - 用户池逐组 `ES_VB_CreatePool` + `ES_VDEC_AttachVbPool`。
- 多 DIE/NUMA 平台建议按 `die-id` 区分 pool 的 `mmzName`，避免跨 NUMA 访问开销（代码已示例）。

3) 线程与性能
- 取帧线程已经绑定 NUMA/CPU 亲和；可结合业务需求调整线程组数 `threadGroupCount`、超时时间、以及 `displayFrameNum/frameBufCnt` 深度。
- 使用 `PerformanceStatic` 打点可快速定位发送/取回/释放瓶颈（已对 `ReleaseFrame` 与 IOVA 释放分别打点）。

4) 码流/帧生命周期
- `ProcessData` 只负责喂码；帧生命周期由 `CImageVd::release()` 完成底层释放。
- 如果在自定义下游中缓存帧，请保证在使用结束后调用图像对象 `release()`，以避免内存泄漏或解码阻塞。

5) Dump 与排障
- 打开 `dump.enable` 可将各通道的输出写入 `elementName_pp_<chn>_dump.raw`；注意性能与磁盘占用。
- 可结合 `ES_VDEC_QueryStatus` 输出组状态（剩余字节、剩余帧、接收状态）定位阻塞位置。

6) 与上游对接
- vdecElement 依赖上游提供的 `VIDEO_STREAM_INFO`（编码类型/宽高），常见上游为 `demuxElement` 或网络 `source`。
- 若定制新上游，请确保 `CVideoPacketMeta::videoPkt` 正确填充 `VDEC_STREAM_S` 字段（包含码流指针、长度、时间戳、`bEndOfStream` 等）。

---

## 6. 参考代码位置
- 元素核心：`src/elements/vdecElement/vdecElement.h`, `vdecElement.cpp`
- VDEC 封装/通用：`src/elements/vdecElement/common/pl_comm_video.c`, `pl_comm_dec.h`, `pl_option_dec.c`
- ES_ 头文件：`es_vdec.h`, `es_vb_memory.h`, `es_comm_video.h`, `es_type.h`

如需将本 README 补充更多实测案例（如特定码率/分辨率的性能建议、常见错误码对照、端到端 pipeline 样例），请告诉我你的设备与输入特征，我可以按需补全。
