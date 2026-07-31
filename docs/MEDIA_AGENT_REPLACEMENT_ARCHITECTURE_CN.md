# Media-Agent 替代架构说明

## 1. 文档目的

本文详细说明当前 EIC7700 Pipeline 替代 `media-agent` 的实现架构。

工程保留官方 Pipeline 的硬件数据通路，包括 RTSP 接入、硬件解码、预处理、
NPU 推理和 DSP 后处理；仅在本工程中实现平台业务能力：

- 接收平台配置并回复 ACK；
- 心跳和任务数量统计；
- 任务启动、停止和模型更新；
- pkg 解密与运行配置生成；
- 跟踪和事件判断；
- 截图和视频证据；
- 告警中继和平台上报。

Pipeline 不链接 `media-agent` 源码，也不迁入其解码器、推理调度器、CPU
后处理或 RTSP 录像模块。

## 2. 部署和进程模型

平台模式只部署一个可执行文件：

```text
pipeline_agent
```

同一个二进制承担两种角色：

```text
pipeline_agent
├── 管理进程
│   ├── 平台 Protobuf/UDS 客户端
│   ├── 增量任务期望状态
│   ├── worker 调和
│   ├── 心跳
│   └── 告警转发
└── pipeline_agent --worker
    ├── 一路设备任务的独立执行单元
    └── 官方 Pipeline 插件图
```

管理进程拥有全部子进程生命周期。worker 通过 `fork + exec` 启动；停止时先
发送 `SIGTERM` 并等待，只有超过宽限时间才使用 `SIGKILL`。

`espl_launch` 继续服务旧静态 case，但不是平台模式依赖。

## 3. 总体数据流

```text
smart-guard-edge
       |
       | Unix Stream Socket，带帧头的 Protobuf
       v
+--------------------------+
| pipeline_agent 管理进程  |
| IpcClient / TaskManager  |
+-----+--------------------+
      | fork/exec worker
      v
+------------------------------------------------------------------+
| EsAvDemux                                                       |
|    | 压缩码流包                                                   |
|    +--> EsEvidenceRecorder --> 每路 GOP/录像状态                   |
|    |                                                             |
|    v                                                             |
| EsVdec -> EsMux -> EsFrameFork                                  |
|                    +-> 有界模型分支 1 -> Event/Sink                 |
|                    +-> 有界模型分支 N -> Event/Sink                 |
+------------------------------------------------+-----------------+
                                                 |
                                                 | AlarmInfo 数据报
                                                 v
                                      管理进程 AlarmRelayServer
                                                 |
                                                 v
                                            平台发送队列
```

平台图不包含 `EsVideoSink`，因此运行不依赖显示器、DRM plane 或显示模式。

## 4. 平台协议

协议定义：

```text
src/business/proto/media-agent.proto
```

传输帧：

```text
4 字节 Magic 0xDEADBEEF
4 字节大端 Protobuf 长度
序列化 Envelope
```

接收：

- `MSG_CONFIG`：增量启动、更新或停止任务；
- `MSG_ALG_MODEL_UPDATE`：重载全部或指定场景模型。

发送：

- `MSG_ACK`；
- `MSG_HEARTBEAT`；
- `MSG_ALARM`。

`SocketSender` 负责自动重连，并使用独立收发线程。Pipeline 数据回调不直接
写平台 stream socket。

## 5. 增量任务状态

平台通常每条消息只下发一路设备，因此 `AgentConfig` 按增量处理，而不是全局
快照。

管理进程维护：

```text
active_streams: map<stream_id, StreamConfig>
```

合并语义：

```text
enabled=true  -> 插入或更新该 stream_id
enabled=false -> 删除该 stream_id
消息未出现    -> 保留原状态
```

连续配置使用防抖：

- 空闲达到 `config_debounce_ms` 后应用；
- 或等待达到 `config_max_wait_ms` 后强制应用。

这样可以避免平台短时间启动多路时重复重建 worker。

## 6. worker 标识和调和

执行单元 key：

```text
stream_id
```

一个任务只有一个 worker、一个 RTSP 会话和一个 VDEC group。key 明确禁止
跨设备按算法聚合；一路设备故障、停止、URL 修改、事件列表或 pkg 更新只重启
该路 worker，不能重启其他设备。

管理进程同时保存序列化 `StreamConfig` 作为签名：

1. 停止期望集合中已经不存在的 worker；
2. key、签名和 PID 均未变化则保留；
3. 只重启配置变化的 worker；
4. 只创建新增 worker；
5. 模型更新只重载选中的场景。

一个平台任务包含一路设备和多个算法。worker 在内部按模型组创建一个或多个
异步推理分支，不再为每个事件重复拉流和解码。

## 7. VDEC group 分配

每个 worker 获得不重叠的 VDEC group 区间。其他任务增删时，已有 worker
保持原 offset。

分配器在 `[0, 128)` 中寻找第一个连续空闲区间，worker 退出后归还。

稳定分配用于：

- 单路任务更新；
- 将 `/proc/esmap/dec` 数据对应到 worker；
- 避免 group 冲突；
- 避免全局解码重启。

## 8. pkg 和模型生命周期

`AlgorithmConfig.model_config_name` 是 pkg 绝对路径。

worker 启动流程：

1. 解密并校验 pkg；
2. 提取到 worker 独立运行目录；
3. 读取根 JSON；
4. 定位 pkg 声明的 `.model`；
5. 读取预处理、类别、阈值和输出尺度；
6. 按 `config/ModelGroups.yaml` 声明和 `.model` 内容指纹聚合模型；
7. 为每个模型组生成独立的 PreProcess/Infer/PostProcess/Tracker/Event 配置；
8. 启动单解码、多模型分支插件图。

pkg 是模型数据的唯一来源，不存在板端 `.model` fallback 或硬编码替换。

EIC7700 YOLOv8 检测 pkg 包含：

- 运行时 JSON；
- 编译后的 `.model`；
- `esquant/table.json`；
- 输出顺序文件；
- manifest。

当前 `ES_AK_DSP_DetectionOut` 接口：

- 恰好三个 NCHW 输出；
- stride 8、16、32；
- 通道数 `64 + class_count`；
- 输出全部为有符号 16 位；
- 每个输出包含 `int16_step`；
- 输出原始 DFL box logits 和 class logits；
- Sigmoid 由 DSP 后处理执行。

旧的 box/class 六输出模型不再支持。

### 8.1 模型组配置

`config/ModelGroups.yaml` 维护事件到模型组的映射、每组队列深度和抽帧间隔。
当前 `general-object-detection` 包含：

```text
area-intrusion, area-loitering, crowd-gather, people-leave,
people-running, vehicle-reverse, vehicle-parking
```

同一模型组只创建一个 NPU 上下文，推理结果交给该组全部事件规则。未声明事件
按解密后 `.model` 内容指纹自动聚合。板端历史 1.1/1.2 pkg 的模型二进制存在
差异时，显式组可设置 `require_identical_model: false`，并通过
`canonical_scenario` 固定规范模型；程序会记录指纹差异。生产切换新模型前
必须确认类别、输入尺寸和 DSP 后处理契约兼容。

## 9. worker 运行目录

每个 worker 在管理进程运行目录下拥有：

```text
<runtime>/stream_<stream-id>_<stable-id>/
├── model_1/ ... model_N/
├── labels_1.txt ... labels_N.txt
├── agent-config_1.pb ... agent-config_N.pb
├── EsAvDemux.yaml
├── EsVdec.yaml
├── EsPreProcess_1.yaml ... EsPreProcess_N.yaml
├── EsInfer_1.yaml ... EsInfer_N.yaml
├── EsPostProcess_1.yaml ... EsPostProcess_N.yaml
├── EsTrackerLite_1.yaml ... EsTrackerLite_N.yaml
└── EsEvent_1.yaml ... EsEvent_N.yaml
```

仅在该 worker 启动或重建时重新生成。

配置映射：

| 平台/pkg 字段 | worker 配置 |
| --- | --- |
| `rtsp_url`、`stream_id` | `EsAvDemux.yaml` |
| 模型输入尺寸 | VDEC scale、PreProcess 输出 |
| padding 参数 | PreProcess letterbox |
| pkg `.model` | `EsInfer_N.yaml` |
| 类别名 | `labels_N.txt` |
| 阈值、输出尺度 | `EsPostProcess_N.yaml` |
| 模型组对应事件列表 | `agent-config_N.pb` |
| 截图、录像目录 | Evidence 配置 |

每个模型组的抽帧间隔和有界队列深度由 `ModelGroups.yaml` 配置。默认每三帧
放行一帧，队列满时丢弃最旧帧；慢模型只降低自身采样率，不阻塞解码线程或
其他模型分支。

## 10. worker 插件图

```text
EsAvDemux
  -> EsEvidenceRecorder
  -> EsVdec
  -> EsMux
  -> EsFrameFork
       ├-> EsQueue(drop-oldest) -> Pre/Infer/Post/Tracker/Event -> Sink
       ├-> EsQueue(drop-oldest) -> Pre/Infer/Post/Tracker/Event -> Sink
       └-> ...
```

`EsFrameFork` 只共享只读的解码图像 FD；每个分支创建独立 frame/batch
元数据，检测对象、推理输出和跟踪状态互不覆盖。原始帧由引用计数延长到最后
一个分支释放，不复制 1080P 图像。

VDEC 只保留一个原始分辨率 NV12 输出。每个模型分支从同一个原图 FD
独立完成缩放和 letterbox，不为首个模型额外创建 VDEC 缩放输出及 VB 池。

### 10.1 EsAvDemux

拉取 RTSP，并传递稳定 `stream_id`、关键帧标记和码流时间信息。

### 10.2 EsEvidenceRecorder

在解码前接收压缩包，按媒体 PTS 等待检测元数据并注入与 `media-agent`
兼容的 MOSP `user_data_unregistered` SEI。它维护录像状态，不建立第二条
RTSP，也不重新编码视频。

### 10.3 硬件推理链

`EsVdec`、`EsPreProcess`、`EsInfer` 和 `EsPostProcess` 使用 EIC7700
VDEC、NPU 和 DSP。

### 10.4 EsTrackerLite

将检测结果适配到工程内独立 ByteTrack 实现。跟踪状态按 stream 和 worker
生命周期隔离。

### 10.5 EsEvent

从固定任务配置读取阈值、ROI、日期、告警等级和去重参数。只有事件满足条件
才生成 `AlarmInfo`。

### 10.6 EsTestSink

结束服务图，不产生显示依赖。

## 11. 证据架构

### 11.1 视频

视频证据使用 `EsAvDemux` 压缩包：

- 保留关键帧和时间信息；
- 缓存告警前 GOP；
- 追加告警后码流；
- remux 为 MPEG-TS；
- 不重复解码和编码；
- 每路独立录像状态。

该路线对 CPU、NPU、VENC 和内存带宽影响较小。

检测框不使用 `EsOsd` 烧录到解码帧。`EsOsd` 会原地修改共享帧，并要求持续
VENC 才能得到带框录像，增加 VENC/VB 和内存带宽，还会让快模型等待慢模型。
当前方案复用 `media-agent` 的 MOSP SEI 协议，将 track id、类别、置信度和
归一化目标框附加到原压缩码流，平台播放器按协议渲染。

录像在触发时直接创建最终 `.ts` 路径，并在每个 packet 后 `avio_flush`。
告警不再先上报一个尚不存在的隐藏临时文件；这修复了区域入侵录像偶发无法
播放，而安全帽录像因访问时序碰巧正常的竞态。

### 11.2 截图

事件插件选择原始分辨率帧，不保存 512x512 推理张量。检测框根据 letterbox
比例和 padding 映射回原图。

当前 `EvidenceService` 映射 NV12 并使用 FFmpeg 软件 JPEG。它只处理告警帧，
不会连续编码，并在复制出的 YUV 帧上绘制目标框、有效 track id（未匹配时
显示 `#NA`）、类别和置信度。它不会修改供其他模型推理使用的共享解码帧，但告警集中时仍
可能产生 CPU 和内存带宽峰值。

按需硬件 JPEG worker 是后续优化项。

## 12. 告警链路

```text
EsEvent
  -> 构造 AlarmInfo
  -> AlarmRelayClient（Unix 数据报）
  -> 管理进程 AlarmRelayServer
  -> IpcClient 有界发送队列
  -> 平台 Protobuf socket
  -> 平台持久化和发布
```

本地数据报隔离 worker 生命周期和平台连接。worker 不直接写共享平台 socket。

截图和录像文件名在进入 relay 前写入 `AlarmInfo`。

## 13. 线程和背压

控制面与数据面分离：

- 平台接收线程解析并入队配置；
- 管理循环调和期望状态；
- worker Pipeline 线程处理媒体数据；
- 证据工作尽量离开 NPU/DSP 回调；
- 告警使用本地数据报；
- 平台发送使用有界队列。

推理回调不能等待平台重连。

当前有界资源：

- 平台发送队列；
- Pipeline Queue 深度；
- VDEC、Mux、PreProcess、Infer 输出 pool；
- 每个 worker 和 VDEC group；
- 证据码流和录像状态。

## 14. MMZ 和 pool

MMZ/VB 不体现在 worker RSS 中，必须通过 `/proc/eswin/vb` 监控。

默认 pool：

| Pool | 默认值 |
| --- | ---: |
| VDEC | 2 |
| Mux | 8 |
| PreProcess | 12 |
| Infer output | 8 |

这些值按 worker 分配，调整时必须乘以最大任务数评估。

生命周期：

1. 管理进程启动 worker；
2. worker 创建媒体 pool；
3. worker 收到 `SIGTERM`；
4. 插件停止并释放 VB；
5. 管理进程等待退出；
6. VDEC offset 可复用。

在旧 worker 退出前创建替代实例会临时占用双倍 MMZ，当前实现避免这种顺序。

## 15. 性能模型

25 FPS 输入、预处理间隔 3：

```text
每路 VDEC：约 25 FPS
每路 NPU 输入：约 8.33 FPS
13 路 VDEC 名义值：325 FPS
13 路 NPU 输入名义值：108.3 FPS
```

NPU/DSP 异步提交，短窗口 `es_hw_watcher` 和 `top` 波动属于预期。吞吐应使用
长窗口累计值判断。

生产模式保持较低 worker 日志级别，逐元素性能输出仅在排查时显式启用。

## 16. 故障隔离

worker 退出后管理进程保持运行，回收子进程并再次调和。

当前保证：

- 单路任务更新不覆盖其他活动任务；
- 停止一路不向其他 worker 发送信号；
- VDEC group 不重叠；
- 模型解密目录不重叠；
- 跟踪和事件状态不跨 worker；
- 平台重连不停止推理 worker。

RTSP、pkg、模型、pool 和插件启动错误只影响对应 worker。

## 17. 可观测性

管理日志：

- 平台连接和重连；
- 配置接收和 ACK；
- 增量合并和活动任务数；
- worker 启停、超时和退出；
- 期望/实际状态调和；
- 模型更新；
- 心跳和告警发送失败。

系统接口：

- `/proc/eswin/vb`：MMZ/VB；
- `/proc/esmap/dec`：VDEC 累计值；
- `/opt/eswin/bin/es_hw_watcher`：VDEC/NPU/DSP；
- `pidstat`：长窗口 CPU；
- 平台 service journal：告警接收、持久化和发布。

## 18. 权限要求

Pipeline 应以开发板普通用户运行，需要：

- 可写平台 UDS；
- 可读模型 pkg；
- 可写运行目录；
- 可写截图和录像目录；
- 可执行 Pipeline 二进制并可读取动态库。

以 root 运行不能替代正确的 socket 和目录权限。

解密模型只存在于 worker 运行目录；worker 目录重建或运行树清理时删除。

## 19. 当前限制

- 变化的任务通过重启其 worker 生效，尚未原位热切换 graph branch；
- 同一路只解码一次，但确实使用不同模型的事件仍分别占用 NPU 上下文；
- MOSP SEI 需要平台播放器支持；通用播放器可播放视频但不会自动渲染框；
- 截图仍使用软件 JPEG；
- 告警发送有有界内存队列，但没有持久化磁盘 spool；
- 录像时间轴依赖上游完整传递 PTS/DTS；
- 存储配额和启动恢复仍需生产强化；
- 心跳当前把活动期望任务计为成功，尚未提供详细 worker 健康状态。

## 20. 验收测试

1. 通过平台启动、停止一路；
2. 新增第二路时第一路 PID 不变；
3. 停止和恢复一路时其他 VDEC 累计值连续；
4. 最大目标路数运行并监控 MMZ；
5. 全部停止后 VB pool 回收；
6. 模型符合三个 S16 输出；
7. 截图保持原始分辨率；
8. 视频片段可播放；
9. 告警收到、持久化并发布；
10. 平台 socket 重连；
11. worker 崩溃隔离；
12. 指定场景模型更新；
13. 长时间 RTSP 稳定性测试；
14. 检查所有队列和日志无持续背压。

完整操作见 `case/platform/run.md`。
