# v4l2SrcElement_readme

本文档面向使用者与二次开发者，帮助你：
- 快速理解 `v4l2SrcElement` 的职责与核心逻辑；
- 熟悉本元素涉及的真实 ES_ SDK API 调用；
- 提供两种使用方式：编码方式构建 Pipeline 与通过 `espl_launch` 构建；
- 指导如何基于源码定制和扩展视频采集插件。

---

## 1. 功能与定位
`v4l2SrcElement` 负责从 Linux V4L2 设备（如 `/dev/video0`）以 DMABUF 零拷贝方式采集视频帧，并将帧以 `CFrameMeta`/`CImage` 形式向下游发送。

- 输入：无（Source 元素）。
- 输出：`CFrameMeta`（`images[1]` 携带 `VIDEO_FRAME_INFO_S`，包含帧宽高、像素格式、fd/stride/offset/PTS 等信息）。
- 线程模型：元素内部创建采集线程 `m_testsrcThread`，`Start()` 启动采集、`Wait()` 等待线程退出、`Finish()` 做资源回收。
- 下游数量：当前实现在 `Init()` 中要求仅有一个下游（`m_NextElementVec.size() == 1`）。
- 零拷贝：通过 ES VB 内存池预分配 N 个块，并将其 fd 以 DMABUF 入队 V4L2，采集出的帧直接引用这些 fd，避免 CPU 拷贝。

典型用途：USB/PCIe 摄像头采集、传感器直连采集，将 NV12/YUY2 帧送入后续预处理/推理/OSD/编码等元素。

---

## 2. 源码结构与核心流程
目录：`pipeline/src/elements/v4l2SrcElement/`
- 头/源文件：`v4l2SrcElement.h/.cpp`
- 配置：`v4l2SrcElement_config.yml`
- 工厂导出：`createEsV4l2SrcElement(const char* name, const char* path, int dieIndex)`

核心流程：
1) Init()
   - 解析 YAML：`devpath,width,height,fps,pixel_format(buf_count,totalframe 可选), dump.enable`。
   - 计算帧大小，创建 ES VB pool（预分配块）；`mV4l2FdArray` 取出 `buf_count` 个 fd 以备 DMABUF。
   - 打开 V4L2 设备，设置格式 `VIDIOC_S_FMT`，设置/查询帧率 `VIDIOC_S_PARM/VIDIOC_G_PARM`，请求缓冲 `VIDIOC_REQBUFS(V4L2_MEMORY_DMABUF)`；
     逐个 `VIDIOC_QBUF` 入队，将我们预分配的 `fd` 提交给驱动使用；记录实际 `fps`。
   - 初始化 `MetaPool<V4l2FrameMeta>`、`MetaPool<CImageV4l2>` 对象池。

2) Start() / threadFunc()
   - `VIDIOC_STREAMON` 开启采集，循环 `VIDIOC_DQBUF` 出队；
   - 为每帧创建/复用 `VIDEO_FRAME_INFO_S`，填充 `width/height/stride/offset/pixelFormat/PTS`，并将 `fd = mV4l2FdArray[buf.index]`；
   - 构造 `CImageV4l2` 与 `V4l2FrameMeta`，设置 `vbIndex/vfd/padIndex/index`，`TransMitToNextToProcess` 送下游；
   - 可选 dump：使用 `ES_SYS_Mmap/ES_SYS_Munmap` 将当前帧映射到 CPU 侧并写文件（仅调试）。
   - 按需根据 `totalframe` 在达到阈值后发送 EOS（`eosFlag=true`）。
   - 退出前 `VIDIOC_STREAMOFF`。

3) V4l2FrameMeta::release()
   - 元数据释放时重排队：将对应 `vbIndex` 的缓冲以 `VIDIOC_QBUF` 重新入队（继续被驱动采集）。
   - 调用基类 `CFrameMeta::release()` 完成常规资源回收。

4) Finish()
   - 回收对象池；释放所有 VB 块并销毁池；关闭设备；释放元素对象本身（NUMA 绑定与释放）。

---

## 3. 真实 ES_ SDK API 列表（按功能分类）
本元素直接/间接使用的 ES_ API（代码中经 `PL_ES_` 封装调用，已溯源到真实 ES_ 名称）：

- VB 内存池与块管理（来自 `es_vb_memory.h`/`es_sys_memory.h`）：
  - ES_VB_CreatePool, ES_VB_DestroyPool
  - ES_VB_GetBlock, ES_VB_ReleaseBlock
  - ES_VB_AllocIOVA（在封装中可选获取 IOVA，用于硬件直通场景）
- 系统内存映射（来自 `es_sys.h`）：
  - ES_SYS_Mmap, ES_SYS_Munmap

相关 ES_ 结构/枚举（数据接口约定）：
- `VIDEO_FRAME_INFO_S`, `VIDEO_FRAME_S`
- `PIXEL_FORMAT_E`（如 `PIXEL_FORMAT_NV12`, `PIXEL_FORMAT_YUY2`）
- `VB_POOL_CONFIG_S`, `VB_POOL`
- `SYS_CACHE_MODE_E`（如 `SYS_CACHE_MODE_NOCACHE`）

说明：
- 源码中出现的 `PL_ES_VB_CreatePool/PL_ES_VB_GetBlock/PL_ES_VB_ReleaseBlock/PL_ES_VB_DestroyPool` 等封装，位于 `pipeline/src/core/src/pl_mem_wrap.cpp`，其内部直接调用了上述真实 ES_ API。
- Linux V4L2 的 `ioctl(VIDIOC_*)` 系列为内核接口，并非 ES_ SDK API，但与 ES VB/DMABUF 协同完成零拷贝链路。

---

## 4. 配置文件说明（v4l2SrcElement_config.yml）
关键键位（与源码一致）：
- `devpath`: string，设备路径（例如 `/dev/video0`）。
- `pixel_format`: string，`nv12` 或 `yuyv|yuy2`。
- `width`, `height`: int，采集分辨率。
- `fps`: int，目标帧率；设备不一定支持强制设置，实际帧率以 `VIDIOC_G_PARM` 查询为准。
- `buf_count`: int，驱动环形缓冲数量（同时也是我们预分配 VB 块数）。
- `totalframe`: int，可选，0 或不配表示持续采集；>0 时到达阈值发 EOS 并退出。
- `dump.enable`: bool，是否 mmap 并保存 yuv 帧到文件（仅调试）。

示例可参考仓库自带 `v4l2SrcElement_config.yml`。

---

## 5. 使用方法一：编码方式构建 Pipeline
以下代码片段展示如何以编码方式创建 `v4l2SrcElement` 并与下游链接（示例仅示意流程，按你的实际工程类名/头文件调整）：

```cpp
#include "pl_pipeline.h"
#include "v4l2SrcElement.h"
// 以及你需要的下游元素头文件，例如 mux/osd/preprocess 等

int main() {
    // 创建元素
    CElement* v4l2 = createEsV4l2SrcElement("v4l2src1", "/path/to/EsV4l2Src.yaml", /*dieIndex*/0);
    CElement* pre = createEsPreProcessElement("preproc1", "/path/to/EsPreProcess.yaml", 0);
    CElement* osd = createEsOsdElement("osd1", "/path/to/EsOsd.yaml", 0);

    // 链接（v4l2 作为 Source，必须有且只有一个下游）
    v4l2->LinkMany({pre});
    pre->LinkMany({osd});

    // 启动
    v4l2->Init(); pre->Init(); osd->Init();
    v4l2->Start(); pre->Start(); osd->Start();

    // 等待
    v4l2->Wait(); pre->Wait(); osd->Wait();

    // 结束
    v4l2->Finish(); pre->Finish(); osd->Finish();
    return 0;
}
```

要点：
- 确保 `v4l2SrcElement` 的下游数量为 1（与源码约束一致）。
- YAML 配置路径传入工厂函数，由元素在 `Init()` 内自行解析。
- 若需将一路采集分发到多路，可在 v4l2 下游追加 `teeElement` 或 `muxElement`。

---

## 6. 使用方法二：通过 espl_launch 构建
仓库提供了多条 case 脚本可直接参考，尤其是：
- `pipeline/case/odv4l2/odv4l2_pipeline.sh`

该脚本示例片段（截取并简化）：
```sh
espl_launch \
  perfstat_interval 10000000 \
  config_path $case_path/config/ \
  EsV4l2Src -name 'v4l2src1' -path EsV4l2Src.yaml - ! \
  EsMux -name mux1 -timeout 40 -poolsize 16 - \
  ... 后续各元素 ... -
```
说明：
- `EsV4l2Src` 即本元素，`-path` 指向配置文件（相对 `config_path`）。
- 各元素之间用 `- !` 串接；最后以 `-` 结束流水线描述。
- 更多完整拼装方式，请参考 `pipeline/case` 目录下各 `.sh`，以及 `pipeline/src/pl_launch/pl_launch.cpp` 的 `main` 参数解析与元素创建逻辑。

---

## 7. 二次开发与扩展建议
1) 像素格式与步幅
- 目前实现支持 `NV12`、`YUY2`；新增格式时需在：
  - `createVideoFrame()` 与大小计算处补齐 `stride/offset/bytes`；
  - V4L2 `VIDIOC_S_FMT`/实际格式打印处添加 FourCC 解释；
  - dump 时的 `size` 计算也要同步修改。

2) 缓冲策略与零拷贝
- 通过 ES VB 池 + V4L2 DMABUF 实现零拷贝；如需切换为用户态 mmap（V4L2_MEMORY_MMAP）或读写（V4L2_MEMORY_USERPTR），需要重构出/入队逻辑与 `release()` 回调。
- `V4l2FrameMeta::release()` 负责将缓冲重新 `QBUF` 入队，是环形队列健康运转的关键；任何修改需确保异常路径也能 `QBUF` 或安全释放。

3) 设备能力与动态参数
- 可根据实际设备能力扩展：自动查询支持的分辨率/像素格式列表，失败时降级或回退；
- 支持运行时动态调节 `fps` 或分辨率，需要在流停止后重新 `S_FMT` 并刷新 VB/队列。

4) 与 ES_ SDK 更深集成
- 若需要在源头直接对接后续硬件模块（如 VENC/VPROC/NPU），可以在本元素中加入对 `ES_VB_AllocIOVA` 的使用与 IOVA 传递；
- 注意 `SYS_CACHE_MODE` 与缓存一致性，必要时增加 `ES_SYS_FlushCache`（若 SDK 提供）等调用。

5) 调试与性能
- 通过环境变量控制日志级别 `PL_LOG_LEVEL`；
- `dump.enable` 仅用于问题定位，生产环境建议关闭；
- 合理设置 `buf_count` 以平衡延迟与抖动，避免因缓冲过少导致丢帧。

---

## 8. 参考与溯源
- ES_ API 溯源封装：`pipeline/src/core/src/pl_mem_wrap.cpp`
  - `PL_ES_VB_CreatePool` -> `ES_VB_CreatePool`
  - `PL_ES_VB_GetBlock`   -> `ES_VB_GetBlock`
  - `PL_ES_VB_ReleaseBlock` -> `ES_VB_ReleaseBlock`
  - `PL_ES_VB_DestroyPool` -> `ES_VB_DestroyPool`
  - 可选：`ES_VB_AllocIOVA`
- V4L2 流程：`VIDIOC_QUERYCAP/VIDIOC_S_FMT/VIDIOC_G_FMT/VIDIOC_S_PARM/VIDIOC_G_PARM/VIDIOC_REQBUFS/VIDIOC_QBUF/VIDIOC_DQBUF/VIDIOC_STREAMON/VIDIOC_STREAMOFF`
- 典型 Case：`pipeline/case/odv4l2/odv4l2_pipeline.sh`

如需我将 `v4l2SrcElement` 接入你指定的 case 或新增一个最小可运行的 pipeline 示例，请告诉我具体下游元素需求与配置路径，我可以一并补充脚本与 YAML 模板。
