# EIC7700 Vision Pipeline

本项目基于 EIC7700 官方 Pipeline，提供 RTSP 多路视频接入、硬件解码、
图像预处理、NPU 推理、DSP 后处理、目标跟踪、事件判断、证据保存和平台告警
上报能力。

工程既保留官方静态 case，也提供可替代 `media-agent` 的平台运行模式。

## 1. 核心能力

- RTSP、文件和 V4L2 视频输入；
- EIC7700 VDEC 硬件解码；
- letterbox、归一化和抽帧预处理；
- EIC7700 NPU 模型推理；
- `ES_AK_DSP_DetectionOut` YOLOv8 后处理；
- ByteTrack 目标跟踪；
- ROI、阈值、时间和告警去重规则；
- 原分辨率告警截图；
- 基于压缩 GOP 的 MPEG-TS 告警录像；
- Protobuf/Unix Socket 平台通信；
- 单路任务增量启动、停止和模型更新；
- 管理进程统一管理独立 worker；
- MMZ、VDEC、NPU 和 DSP 压力测试工具。

## 2. 平台模式

平台模式只启动：

```text
pipeline_agent
```

管理进程负责平台 IPC、活动任务表、心跳、告警转发和 worker 生命周期；
worker 由同一二进制使用 `pipeline_agent --worker` 运行。

```text
平台 AgentConfig
      |
      v
pipeline_agent 管理进程
      |
      +--> worker: RTSP A + hardhat
      +--> worker: RTSP B + hardhat
      +--> worker: RTSP C + phone
```

每路任务独立增删。更新或停止一路时，其他 worker 的 PID、VDEC group、模型
上下文和事件状态保持不变。

平台操作入口：

- [平台模式简介](case/platform/README.md)
- [完整运行手册](case/platform/run.md)
- [替代架构中文版](docs/MEDIA_AGENT_REPLACEMENT_ARCHITECTURE_CN.md)
- [问题排查与演进记录](docs/update.md)

## 3. 构建

### 3.1 本机构建

```sh
./scripts/build.sh --jobs 8
```

输出：

```text
build/
```

本机构建需要与工程依赖相匹配的开发环境，主要用于编译检查和不依赖板端
硬件的单元测试。

### 3.2 RISC-V 交叉编译

在 `cross-proj` 的 `SDK_VERSION=202606` 容器内执行：

```sh
cd /workspace/proj/pipeline
./scripts/build.sh \
  --target riscv64 \
  --jobs 8 \
  --install
```

输出：

```text
build-riscv64/
build-riscv64/install/
```

主要安装文件：

```text
build-riscv64/install/opt/demo/pipeline/bin/
build-riscv64/install/usr/local/lib/
build-riscv64/install/opt/demo/pipeline/config/
```

## 4. 平台模式快速启动

开发板默认部署目录：

```text
/home/ubuntu/workspace/test/pipeline
```

连接真实平台：

```sh
cd /home/ubuntu/workspace/test/pipeline

./case/platform/run_real_platform.sh --restart
```

不连接真实平台时，执行完整模拟验证：

```sh
./case/platform/run_mock_platform_e2e.sh \
  --channels 3 \
  --churn-index 2 \
  --multi-event-index 1
```

## 5. 模型包

平台通过 `AlgorithmConfig.model_config_name` 下发 pkg 绝对路径。

EIC7700 YOLOv8 检测 pkg 应包含：

- 根运行时 JSON；
- `.model`；
- `esquant/table.json`；
- `.ofmap_order.txt`；
- `manifest.json`。

官方 DSP DetectionOut 要求：

- 3 个 NCHW 输出；
- stride 为 8、16、32；
- 通道数为 `64 + class_num`；
- 输出类型为 `int16`；
- 每个输出包含 `int16_step`。

Pipeline 直接使用 pkg 中的模型和参数，不使用板端固定模型替换。

## 6. 目录说明

### 6.1 根目录

| 目录/文件 | 功能 |
| --- | --- |
| `README.md` | 项目入口和目录说明 |
| `bin/` | 本地预置或部署用可执行文件 |
| `case/` | 静态 case、平台模式和硬件功能示例 |
| `cmake/` | RISC-V 工具链和 CMake 辅助模块 |
| `dataset/` | 示例输入、标签或测试数据 |
| `docs/` | 架构、排查和专题分析文档 |
| `models/` | 静态 case 使用的本地模型与配置 |
| `scripts/` | 构建、格式恢复和压力测试脚本 |
| `src/` | Pipeline 核心源码和顶层 CMake 工程 |
| `tools/` | 板端硬件调试辅助程序 |
| `vendor/` | 工程直接使用的第三方或厂商接口 |
| `video/` | 示例视频或视频测试资源 |
| `build/` | 本机构建输出，不应提交 |
| `build-riscv64/` | 交叉编译输出，不应提交 |

### 6.2 `src/`

| 目录 | 功能 |
| --- | --- |
| `agent/` | `pipeline_agent` 管理进程、任务调和和 worker 启动 |
| `algorithm/` | 跟踪、事件规则和模型包相关独立算法实现 |
| `business/` | Protobuf 协议、平台 IPC、心跳、ACK 和告警中继 |
| `core/` | Pipeline 核心图、元数据和公共基础设施 |
| `elements/` | Demux、VDEC、Infer、PostProcess、Tracker、Event 等插件 |
| `evidence/` | 截图、压缩码流缓存和录像服务 |
| `pl_launch/` | Pipeline 命令行解析和静态图启动逻辑 |
| `vendor/` | 构建所需厂商头文件、库和适配层 |
| `CMakeLists.txt` | 项目实际 CMake 入口 |
| `pipeline_user_manual.md` | 官方 Pipeline 使用说明 |

### 6.3 `case/`

| 目录 | 主要用途 |
| --- | --- |
| `platform/` | 平台任务、告警和增量启停；包含本地模拟平台 |
| `hardhat/` | 固定安全帽模型的独立验证 |
| `od/` | RTSP/文件目标检测和多路压力测试 |
| `odv4l2/` | V4L2 摄像头目标检测 |
| `codec/` | 编解码和转码功能验证 |
| `dualDie/` | 双 Die 或跨 Die 场景验证 |
| `elementTest/` | 单个 Pipeline element 功能测试 |
| `msi/` | MSI 数据路径和相关硬件测试 |
| `nvr/` | NVR、多路录像和播放场景 |

各 case 下的 shell 脚本是实际运行入口。没有显示器的服务场景不要启用
`EsVideoSink`。

### 6.4 `scripts/`

| 文件 | 功能 |
| --- | --- |
| `build.sh` | 本地和 RISC-V 统一构建入口 |
| `check_text_format.py` | 检查文本编码、换行并恢复复制后权限状态 |
| `mmz_increment_server.py` | 旧的固定 13 路 MMZ 增量回归服务器 |
| `run_rtsp_stability.sh` | 多路 RTSP 长时间稳定性测试 |

通用任务启停测试优先使用：

```text
case/platform/mock_platform.py
case/platform/run_mock_platform_e2e.sh
```

## 7. 运行时目录

平台 worker 默认生成：

```text
/tmp/pipeline-agent/<scenario>_<id>/
```

其中包含解密模型、量化表、标签、固定任务 Protobuf 和动态生成的 YAML。

常用日志：

```text
/tmp/pipeline-platform.log
/tmp/pipeline-mock-agent.log
/tmp/pipeline-mock-server.log
```

## 8. 状态检查

```sh
# 管理进程和 worker
pgrep -a pipeline_agent
pgrep -fc 'pipeline_agent --worker'

# MMZ/VB
cat /proc/eswin/vb

# VDEC
tr -d '\000' </proc/esmap/dec |
sed -n '/GRP STATUS/,/CHN STATUS/p'

# NPU/DSP/VDEC 短窗口
sudo /opt/eswin/bin/es_hw_watcher -i 2 -c 15

# CPU 长窗口
pids=$(pgrep -x pipeline_agent | paste -sd, -)
pidstat -u -p "$pids" 1 20
```

## 9. 开发原则

- 平台消息按 `stream_id` 增量合并；
- 单路变化不能重启其他路；
- pkg 是模型和参数的唯一来源；
- 不在推理回调中执行平台或磁盘阻塞 I/O；
- 不为录像建立第二条 RTSP；
- MMZ 通过 `/proc/eswin/vb` 评估，不能只看 RSS；
- worker 停止后确认 VB 释放再创建替代实例；
- 生产模式关闭逐帧 DEBUG 和不必要的性能落盘；
- 每次模型更新必须验证三个 S16 输出 ABI。

## 10. 已知限制

- 任务配置变化通过重启对应 worker 生效；
- 同一路多事件可能加载多个模型上下文；
- 告警截图当前使用按需软件 JPEG；
- 告警发送暂未实现持久化磁盘 spool；
- 心跳成功数量尚未细分 worker 实际健康状态；
- 存储配额、异常掉电恢复和录像时间轴仍需持续强化。

详细设计和问题背景见 `docs/`。
