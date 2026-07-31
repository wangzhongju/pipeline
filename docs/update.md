# Pipeline 平台替代项目问题排查与演进记录

本文记录 Pipeline 从官方示例工程演进为可替代 `media-agent` 的平台视频分析
服务过程中，已经遇到的主要问题、排查方法、根因、修复方案和验证结果。

本文不是发布日志。它重点解释为什么形成当前架构，以及后续遇到相似现象时
应从哪里开始检查。

## 1. 项目目标和约束

目标是在 EIC7700 上保留官方 Pipeline 的硬件数据通路，并迁入
`media-agent` 的平台业务能力：

- 接收平台任务和模型更新；
- 支持每个任务独立启动、停止和更新；
- 解密事件 pkg 并加载其中的 EIC7700 模型；
- 跟踪、事件判断和告警去重；
- 保存原分辨率截图和视频片段；
- 将告警、截图和录像信息上报平台；
- 多路运行时保持其他任务不受单路增删影响。

明确不迁入的内容：

- `media-agent` 自有 RTSP 拉流、解码和推理调度器；
- CPU YOLO 后处理替代官方 DSP 后处理；
- 为录像再建立一条 RTSP 连接；
- 推理回调中的同步平台或磁盘 I/O。

## 2. 交叉编译工程适配

### 2.1 现象

最初 `test_pipeline` 和后续 `pipeline` 是从服务器复制到本地的普通工程，
不能直接复用外层 `cross-proj` 的 SDK 选择、sysroot 和工具链。

从 Linux 服务器使用 SCP 复制到 Windows 后，还出现了文件可执行权限和换行符
状态变化，文本格式检查会把正常的权限差异误判为源码异常。

### 2.2 排查

对照可正常交叉编译的 `proj/media-agent`，检查：

- `scripts/build.sh` 的目标参数和固定构建目录；
- `cmake/riscv64-toolchain.cmake`；
- 外层 `SDK_VERSION=202606` 环境；
- 容器内源码路径和宿主机映射；
- Git index 中记录的可执行位；
- CRLF/LF 和 UTF-8 编码。

### 2.3 修复

- Pipeline 使用 `scripts/build.sh --target riscv64 --jobs 8 --install`；
- RISC-V 构建固定输出到 `build-riscv64`；
- 安装树固定输出到 `build-riscv64/install`；
- 工具链由外层 202606 SDK 环境提供；
- `scripts/check_text_format.py` 按 Git index 恢复脚本可执行位和文本状态，
  不依赖 SCP 后的 Windows 文件权限。

### 2.4 验证

```sh
SDK_VERSION=202606 ./docker/docker.sh start

docker exec -u 1000:1000 cross-proj-202606-cdky \
  bash -lc '
    cd /workspace/proj/pipeline &&
    ./scripts/build.sh --target riscv64 --jobs 8 --install
  '

file proj/pipeline/build-riscv64/install/opt/demo/pipeline/bin/pipeline_agent
```

输出应为 RISC-V 64 位、LP64D ABI。

## 3. 板端最小部署与无显示器运行

### 3.1 现象

官方 OD case 末端包含 `EsVideoSink`。开发板没有连接显示器时，显示设备、
DRM plane 或输出模式初始化可能失败，并导致整条图启动失败。

### 3.2 决策

平台生产链路不需要显示输出。显示插件只用于本地演示，不应成为推理服务的
运行依赖。

### 3.3 修复

平台 worker 的末端使用 `EsTestSink`，不创建 `EsVideoSink`。部署只复制：

- `bin/pipeline_agent`；
- Pipeline 运行需要的动态库；
- `Event.yaml`、`EsTracker.yaml`；
- `case/platform` 启动和测试脚本。

模型不复制到固定 `models/` 路径，由平台任务通过 pkg 绝对路径提供。

## 4. `media-agent` 性能差异分析

### 4.1 现象

同一路视频和模型下，`media-agent` 的性能明显低于官方 Pipeline，压力较高时
还出现流阻塞。

### 4.2 排查方向

比较两套程序的数据路径：

```text
media-agent:
RTSP -> 自有队列 -> 解码 -> 推理调度 -> CPU/自定义后处理 -> 业务线程

Pipeline:
EsAvDemux -> EsVdec -> EsMux -> EsPreProcess -> EsInfer
          -> EsPostProcess(DSP) -> EsTrackerLite -> EsEvent
```

重点检查：

- 是否存在多次内存复制或格式转换；
- 是否在推理线程同步写磁盘或发送平台消息；
- 是否为录像重复拉流；
- 后处理是在 CPU 还是 DSP；
- 队列满时是丢弃、合并还是阻塞上游；
- 调试日志和性能落盘是否逐帧执行。

### 4.3 架构结论

保留官方 Pipeline 的解码、预处理、NPU 推理和 DSP 后处理，只迁移平台协议、
事件和证据业务。业务模块不反向依赖 `media-agent` 源码或动态库。

## 5. 从两个可执行文件收敛为一个

### 5.1 初始问题

早期方案使用一个程序连接平台，另一个程序运行 Pipeline。部署、停止、异常
恢复和 PID 管理都比 `media-agent` 复杂。

### 5.2 当前方案

只有一个 `pipeline_agent`：

- 无 `--worker` 参数时运行管理进程；
- 管理进程使用 `fork + exec` 启动
  `pipeline_agent --worker`；
- worker 运行官方 Pipeline 插件图；
- 管理进程统一负责所有 worker 的增删、重启和回收。

这样既保留进程级资源隔离，又只有一个部署入口。

## 6. 平台连续下发任务只剩一路

### 6.1 现象

任务 A 正在运行时，平台启动任务 B，A 被停止。只有在
`pipeline_agent` 启动前平台一次下发多路全量配置时，才能同时运行多路。

### 6.2 根因

管理进程把每条 `AgentConfig` 当成全量快照。平台实际语义是增量任务消息：
一条消息通常只包含一路设备的启动或停止。

每次收到 B 后直接用 B 构建期望图，会自然把未出现的 A 当成需要删除。

### 6.3 修复

维护以 `stream_id` 为主键的 `active_streams_`：

```text
enabled=true  -> active_streams_[stream_id] = stream
enabled=false -> active_streams_.erase(stream_id)
```

合并完本批增量后，再从完整活动表构建期望 worker 集合。没有出现在本次消息
中的任务保持不变。

增加配置防抖：

- 连续消息在 `config_debounce_ms` 内合并；
- 等待不超过 `config_max_wait_ms`；
- 避免平台短时间启动多路时反复构图。

### 6.4 验证

日志中的 `active_streams` 应按 1、2、3 递增。停止 B 后只减少一，不影响 A。

## 7. worker 隔离策略的演进

### 7.1 “按算法跨设备聚合一个 worker”的问题

早期为了提高批处理效率，尝试把使用同一算法或模型的多路设备聚合进一个
worker。该方案存在以下问题：

- 新增一路会改变聚合组完整配置，导致组内所有流重启；
- 停止一路同样需要重建整个聚合组；
- 一个 RTSP、VDEC 或插件异常会影响组内其他设备；
- VDEC group、VB pool 和事件状态难以按任务独立回收；
- 无法满足平台“单路任务启停不影响其他任务”的业务要求。

### 7.2 改为每路任务独立

隔离边界改为单路任务。一个 `pipeline_agent` 管理所有任务，每路任务有独立：

- worker 进程；
- RTSP demux 和 VDEC group；
- 运行时配置目录；
- 模型和量化表解密目录；
- 跟踪与事件状态；
- 证据缓存和录像会话。

当前内部 key 进一步包含：

```text
stream_id + scenario + pkg
```

因此一个任务包含多个不同事件/pkg 时，每个事件执行单元独立。这里所说的
“每路任务一个独立 worker”强调不再跨设备聚合；多事件任务可能拥有多个
worker，但都由同一个管理进程维护。

### 7.3 代价

- 模型上下文和 pool 不能跨任务共享；
- 进程数增加；
- 单模型多流批处理能力降低。

在当前平台语义和 EIC7700 插件资源生命周期约束下，隔离和可回收性优先于
跨设备聚合。后续若实现不重启的动态 branch API，再评估进程内共享模型。

## 8. 单路增删导致其他任务重启

### 8.1 现象

已有多路运行时，新增或停止任一路，其他路的 worker PID、VDEC group 和
推理上下文也发生变化。

### 8.2 根因

worker 调和逻辑只比较“组是否存在”，没有稳定比较每个执行单元的序列化配置；
或 worker key 中缺少 `stream_id`，导致一个任务变化被解释成整个算法组变化。

### 8.3 修复

- 使用稳定 key 标识 worker；
- 保存 worker 的 `StreamConfig.SerializeAsString()` 作为签名；
- key 和签名均未变化时保留原 PID；
- 先停止已不需要的 worker，再只创建新增或变化的 worker；
- VDEC group 使用空闲区间分配，不重新编号现有 worker。

### 8.4 验证

停止第 7 路前后记录所有 worker PID，并检查其他路：

- PID 不变；
- `/proc/esmap/dec` 中累计解码帧继续增长；
- NPU 和 DSP 不出现全局归零后重新爬升。

## 9. MMZ 内存耗尽

### 9.1 现象

平台连续增加任务后，`pipeline_agent` RSS 看起来稳定，但
`cat /proc/eswin/vb` 显示 MMZ/VB 被耗尽。后续 worker 无法创建 pool，
解码和推理停止。

### 9.2 为什么 `top` 看不出来

MMZ/VB 是媒体硬件使用的连续物理内存，不计入普通进程 RSS。只看 `top`
会误判为不存在泄漏。

必须同时观察：

```sh
cat /proc/eswin/vb
cat /proc/esmap/dec
sudo /opt/eswin/bin/es_hw_watcher
```

### 9.3 排查方法

1. 启动前记录 `total size`、`free mem size` 和 pool 列表；
2. 每增加一路记录一次，计算单路增量；
3. 固定其他任务，只停止一路，观察对应 pool 是否释放；
4. 再启动同一路，检查下降量是否与第一次一致；
5. 全部停止后确认 free 恢复；
6. 检查是否存在旧 worker、重复管理进程或僵尸进程；
7. 将 worker 启停日志与 `/proc/eswin/vb` 时间点对齐。

### 9.4 主要原因

问题不是 C++ 堆内存持续增长，而是任务配置处理和媒体资源生命周期叠加：

- 每次增量消息曾重建所有流，旧 worker 释放和新 worker 创建发生重叠；
- 聚合 worker 变化导致无关流也重建；
- VDEC、Mux、PreProcess、Infer 输出 pool 按过大的固定值逐路复制；
- worker 异常退出或强杀后，插件/VB 资源释放需要时间；
- 短时间连续下发造成多个生命周期操作同时进行。

### 9.5 修复

- 增量合并任务，不再全量覆盖；
- 每路任务独立 worker，只重启受影响任务；
- 配置消息防抖；
- worker 停止先发 `SIGTERM` 并等待，超时才 `SIGKILL`；
- worker 回收后再创建替代实例；
- 默认 pool 调整为：

```text
VDEC pool size       2
Mux pool size        8
PreProcess pool size 12
Infer output pool    8
```

- VDEC group 使用稳定区间分配并随 worker 回收。

### 9.6 验证标准

- 任务数量稳定后 `free mem size` 不再持续下降；
- 停止一路后释放量可观测；
- 同一路停止/恢复多次不会形成阶梯式永久下降；
- 全部停止后 Pipeline 创建的 pool 消失；
- 13 路连续启动时没有 `Cannot allocate memory` 或 `create pool failed`。

`peak used size` 是驱动历史峰值，不等于当前使用量，判断当前状态应看
`free mem size` 和活动 pool。

## 10. 13 路时解码停止、未正常推理

### 10.1 排查顺序

1. RTSP 是否能拉取；
2. worker 是否仍存活；
3. VDEC group 是否创建；
4. `DecodeFrmNum` 是否增长；
5. VB/MMZ 是否有空闲；
6. NPU/DSP 是否有提交；
7. Pipeline 日志是否有 pool、模型或后处理失败。

### 10.2 发现

RTSP 恢复后仍需区分输入故障和资源故障。13 路场景中，MMZ 不足会先导致
新 VDEC/pool 创建失败；已有路也可能因全量重启同时失去解码上下文。

### 10.3 修复后的行为

每路 worker 独立后：

- 单路 RTSP 失败不停止其他路；
- 单路增删不重建其他 VDEC；
- 资源不足只会使新增 worker 启动失败，管理进程与已有 worker 保持运行；
- worker 异常退出由管理进程回收和调和。

## 11. CPU 占用和硬件帧率跳动

### 11.1 现象

`top` 中各 worker CPU 在 2% 到 30% 之间跳动；13 路 1080P 25 FPS 时，
`es_hw_watcher` 的 VDEC、NPU Usage 和 Framerate 也明显波动。

### 11.2 原因

- `top` 是短采样窗口，线程工作是突发式；
- RTSP 到帧时间并不严格均匀；
- VDEC、NPU、DSP 异步提交和完成；
- 多 worker 的提交相位不断变化；
- 告警截图、录像关闭和日志会产生短时 CPU/I/O；
- `es_hw_watcher` 默认统计窗口较短。

### 11.3 抽帧逻辑

当前 `EsPreProcess.yaml`：

```yaml
interval: [3, 1]
```

按帧序号每 3 帧放行 1 帧，即第 0、3、6、9... 帧。25 FPS 输入：

```text
每路 NPU 输入约 25 / 3 = 8.33 FPS
13 路约 108.3 FPS
```

VDEC 不抽帧，13 路名义解码总帧率仍为 325 FPS。

### 11.4 正确测量

- CPU 使用 `pidstat 1 20` 看长窗口平均值；
- VDEC 使用 `/proc/esmap/dec` 的累计帧做 30 秒差值；
- NPU/DSP 使用 `es_hw_watcher -i 2 -c 15`；
- 同时检查输入 RTSP 是否丢包或重连；
- 关闭逐帧 DEBUG 和不必要的性能落盘。

## 12. pkg 模型和运行参数

### 12.1 Pipeline 需要的数据

EIC7700 检测 pkg 至少包含：

- 根运行时 JSON；
- 编译后的 `.model`；
- `esquant/table.json`；
- `.ofmap_order.txt`；
- `manifest.json`。

根 JSON 提供：

- 模型类型、文件名、输入尺寸；
- 类别数量和类别名；
- 置信度、IoU、top-k；
- RGB/NCHW、归一化、letterbox 和 padding；
- 后处理类型、stride、DFL `reg_max`；
- 三个输出张量名称、形状、类型和量化尺度。

### 12.2 独立性

`pipeline_agent` 在自己的源码中实现 pkg 解密、校验和文件提取，不依赖
`media-agent` 工程。worker 只使用解密到运行目录的文件。

## 13. 六输出模型不满足 DSP DetectionOut

### 13.1 现象

平台旧 pkg 中 YOLOv8 `.model` 有 6 个输出：三个 DFL box 分支和三个分类
分支。官方 `ES_AK_DSP_DetectionOut` 要求三个融合输入，后处理初始化失败。

### 13.2 临时方案及其问题

曾在代码中检测 6 输出，对安全帽任务改用板端
`260106_hardhat_cls2_512_b1_v1.model`，并硬编码类别、阈值和量化尺度。

这个方案只能临时验证业务链路，存在严重问题：

- pkg 内容与实际推理模型不一致；
- 平台更新模型不生效；
- 类别语义可能不一致；
- 不能扩展到其他检测事件；
- 部署额外依赖板端固定模型。

### 13.3 正式修复

在 EIC7700 量化流水线中修改 ONNX 输出：

```text
stride 8 : concat(64 路 DFL box logits, class logits)
stride 16: concat(64 路 DFL box logits, class logits)
stride 32: concat(64 路 DFL box logits, class logits)
```

严格 ABI：

- 恰好 3 个输出；
- NCHW；
- 通道数 `64 + class_num`；
- 输出类型全部 `int16`；
- 每个输出都有 `int16_step`；
- 分类保持 logits，由 DSP 内部 Sigmoid；
- stride 顺序为 8、16、32。

已重新量化：

```text
climb coco fire garbage hardhat phone sleep
```

每个模型均生成 `.model`、量化表、运行时 JSON 和版本化 pkg。

### 13.4 移除临时补丁

量化模型修复后，删除：

- 六输出检测分支；
- 板端 fallback 模型路径；
- 硬编码类别、阈值和量化尺度；
- `board-fallback` 日志和文档检查。

现在 pkg 是模型和参数的唯一来源。

## 14. 原分辨率截图和视频片段

### 14.1 问题

推理输入是 512x512 letterbox 图。如果直接保存推理输入，证据图片与原始
1080P 视频不一致，目标框映射也可能错误。

### 14.2 当前路线

- 推理继续使用缩放后的输入；
- 告警保存引用原始输入帧；
- 检测框根据 letterbox 比例和 padding 映射回原图；
- 截图以原始宽高保存；
- 视频从 `EsAvDemux` 的压缩包缓存形成 GOP 录像，不重复拉流；
- 视频片段直接 remux，避免再次解码和编码。

这种方案比 `media-agent` 的 CPU NV12 转换和软件 JPEG 更适合当前设备，
但硬件 JPEG worker 和存储配额仍需持续优化。

## 15. 告警不上报

### 15.1 排查链路

告警必须逐级检查：

```text
模型输出
-> DSP 后处理
-> Tracker
-> Event
-> Evidence
-> AlarmRelay
-> pipeline_agent
-> 平台 UDS
-> 平台持久化
-> 平台事件发布
```

不能只看“有推理帧”就判断告警功能正常。

### 15.2 验证日志

Pipeline：

```text
target_detection triggered
event triggered
snapshot saved
record saved
alarm queued
```

平台：

```text
media-agent alarm received
media-agent alarm persisted
media-agent event published
```

新安全帽 pkg 已完成 13 路加载，平台成功持久化并发布告警。

### 15.3 模型语义注意

当前 hardhat 训练模型类别为 `hat/person`，任务目标标签曾配置为 `person`。
这可以验证告警技术链路，但“检测到 person”不等价于“未佩戴安全帽”。
生产语义需要：

- 使用直接输出未戴安全帽类别的模型；或
- 建立 person/head 与 hat 的空间关联事件规则。

## 16. 日志、UDP 和录像时间戳

压力测试中曾出现一次：

```text
bind failed: Address already in use
```

该错误只在启动早期出现一次，后续推理、截图、录像和告警仍持续运行。排查时
应检查每路录像 mux 使用的 UDP/临时端口是否唯一，不能因为告警链路继续工作
就忽略端口冲突。

FFmpeg 还可能报告输入 packet 缺失 PTS/DTS 并自动生成时间戳。录像可播放不
代表时间轴完全正确，后续应确保 demux 到 EvidenceRecorder 全程保留原始
PTS、DTS、duration 和 time_base。

## 17. 当前回归检查表

每次修改管理进程、worker 图、模型 pkg 或 pool 参数后至少检查：

1. RISC-V 交叉编译通过；
2. pkg 可解密，manifest 校验通过；
3. 模型为三个 S16 DetectionOut 输出；
4. 单路启动、停止和恢复正常；
5. 新增一路不改变其他 worker PID；
6. 停止一路不停止其他 VDEC；
7. 13 路连续启动无 MMZ 分配失败；
8. 运行中 MMZ 稳定，停止后 pool 回收；
9. VDEC 累计帧和 NPU/DSP 负载正常；
10. 截图分辨率与原始输入一致；
11. 视频片段可播放且从关键帧开始；
12. 告警收到、持久化并发布；
13. worker 异常退出时管理进程仍存活；
14. 模型更新只影响对应 scenario；
15. 日志中没有长期重复的重连、端口冲突或队列满。

模拟平台操作见：

```text
case/platform/README.md
case/platform/run.md
case/platform/mock_platform.py
```

## 18. 单路多事件、证据标注与区域录像修复

### 18.1 旧架构问题

旧 worker key 为 `stream_id + scenario + pkg`。同一路任务每增加一个事件就
增加一个 RTSP、VDEC、VB pool 和模型上下文。七个通用目标事件即使使用同一
检测模型，也会重复推理；多个不同速度模型还会互相形成无界积压风险。

调整后 worker key 只有 `stream_id`：

```text
一次 RTSP -> 一次 VDEC -> EsFrameFork
                         ├-> 模型组 1 有界丢旧队列
                         ├-> 模型组 2 有界丢旧队列
                         └-> ...
```

`EsFrameFork` 不复制图像，只共享只读 FD 和源帧引用；各分支拥有独立元数据。
慢模型队列满时丢弃自己的最旧帧，不阻塞解码和其他模型。

VDEC 只输出一个原始分辨率 NV12 通道。各模型的 `EsPreProcess` 都从通道 0
读取同一个原图 FD，并按本模型输入尺寸独立缩放和 letterbox；不再保留旧
单模型链路中的额外“首模型尺寸 VDEC 缩放通道”，从而避免为每路流多申请
一套无实际消费者的 VB 图像池。

### 18.2 模型复用

`config/ModelGroups.yaml` 维护事件分组、规范事件、队列深度和抽帧间隔。
七个通用目标事件归入 `general-object-detection`，一个任务内只创建一个
NPU 上下文，推理结果供多个事件规则判断。未配置事件按 `.model` 内容指纹
自动聚合。

板端历史 area-intrusion 1.2 与 area-loitering 1.1 pkg 的 `.model` 指纹
不同，因此显式组使用 `canonical_scenario: area-intrusion`，并记录差异
告警。后续替换 pkg 时仍需验证类别、输入尺寸和 DSP 后处理 ABI。

### 18.3 截图与视频标注方案

对比结果：

- `EsOsd` 适合显示或必须烧录像素的输出，但会原地修改解码帧；多模型共享
  FD 时存在数据竞争，录像还需要持续 VENC，增加 VENC/VB 和带宽占用。
- `media-agent` 的 MOSP SEI 不改视频像素，不重新编码，能在原压缩码流中
  携带 track id、类别、置信度和框，适合当前平台播放器与多模型架构。

因此截图在复制出的原分辨率 YUV 上软件绘制标签，视频复用 MOSP
`user_data_unregistered` SEI。协议规定 track id 0 表示未跟踪：有效 ID
按原值显示，未匹配目标在截图中明确显示 `#NA`，不能伪造为同一个 ID。

### 18.4 区域入侵录像偶发不可播放

原录像先写隐藏临时文件，告警立即上报最终路径，录制结束后才 rename。平台
若在 rename 前读取，得到文件不存在或不完整；安全帽成功只是访问时序碰巧
较晚，区域入侵更容易复现竞态。

修复后触发时直接创建最终 `.ts`，每写一个 packet 调用 `avio_flush`。TS
允许边写边读，平台收到告警时路径已存在，并且已有可探测的 PAT/PMT 和视频
包。

### 18.5 板端验证

2026-07-30 在 EIC7700 板端完成：

- 两路任务、多事件增删共 6 次配置全部 ACK；
- worker 数始终等于设备路数，未修改流 PID 不变；
- `general-object-detection:2` 只创建 `model_groups=1`；
- 区域入侵截图为 1920x1080，包含类别、追踪状态和置信度标注；
- 未获得稳定追踪匹配的目标显示为 `PERSON#NA 0.89`，不再伪造重复 ID；
- 最终回归 TS 为 H.264 1920x1080、5.84 秒、约 4.24 MB，
  `ffprobe` 可完整解析；
- `trace_headers` 在最终回归片段中检测到 43 条
  `User Data Unregistered` SEI；
- stop-all 前后 MMZ free 均为 `0x17ffff000`；
- 全流程 `critical_error_count=0`。
