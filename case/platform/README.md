# Pipeline 平台模式

本目录用于验证 Pipeline 与 `smart-guard-edge` 平台的任务、告警和证据链路，
也可以在不依赖真实平台服务的情况下模拟逐路任务启动与停止。

## 文件说明

| 文件 | 用途 |
| --- | --- |
| `launch_pipeline_agent.sh` | 底层启动一个 `pipeline_agent` 管理进程，连接指定平台 UDS |
| `run_real_platform.sh` | 真实平台模式的生产运行、检查、重启和连接确认入口 |
| `mock_platform.py` | 模拟平台 UDS 服务端并发送 `AgentConfig` |
| `run_mock_platform_e2e.sh` | 参数化执行模拟平台端到端全流程回归 |
| `run.md` | 开发板部署、启动、模拟下发、检查和停止的完整操作手册 |

平台模式只需要部署一个 `pipeline_agent` 可执行文件。管理进程负责：

- 连接平台并接收 `AgentConfig`；
- 维护所有活动任务的期望状态；
- 为受影响的任务创建或停止 worker；
- 接收 worker 告警并转发给平台；
- 定时发送心跳和任务统计。

worker 仍由同一个二进制通过 `pipeline_agent --worker` 启动。`espl_launch`
只服务于旧的静态 case，不是平台模式的必需程序。

## 真实平台快速启动

```sh
cd /home/ubuntu/workspace/test/pipeline

./case/platform/run_real_platform.sh --restart
```

默认连接：

```text
/opt/smart-guard/run/media-agent/media_agent.sock
```

该 socket 必须存在，并允许运行 Pipeline 的用户读写。

## 模拟平台快速启动

推荐直接执行完整自动验证：

```sh
cd /home/ubuntu/workspace/test/pipeline

./case/platform/run_mock_platform_e2e.sh
```

默认验证三路任务、单路停止和恢复、单路多事件增删、截图、录像、告警、
VDEC/NPU/DSP、MMZ 回收，并在结束后恢复测试前的真实平台模式。

13 路任务示例：

```sh
./case/platform/run_mock_platform_e2e.sh \
  --channels 13 \
  --churn-index 7 \
  --multi-event-index 1 \
  --evidence-timeout 180
```

只验证任务生命周期，不等待告警证据和硬件采样：

```sh
./case/platform/run_mock_platform_e2e.sh \
  --evidence-mode off \
  --skip-hardware \
  --mmz-mode observe
```

以下命令用于手工逐步调试。

模拟测试不能与真实平台模式共用同一个管理进程。先停止现有
`pipeline_agent`，再启动模拟平台：

```sh
cd /home/ubuntu/workspace/test/pipeline

python3 case/platform/mock_platform.py serve \
  >/tmp/pipeline-mock-server.log 2>&1 &

PIPELINE_PLATFORM_SOCKET=/tmp/pipeline-mock-platform.sock \
PIPELINE_AGENT_RUNTIME_DIR=/tmp/pipeline-mock-agent \
PIPELINE_ALARM_RELAY_SOCKET=/tmp/pipeline-mock-agent/alarm.sock \
./case/platform/launch_pipeline_agent.sh \
  >/tmp/pipeline-mock-agent.log 2>&1 &
```

启动一路安全帽检测任务：

```sh
python3 case/platform/mock_platform.py start \
  --stream-id camera-01 \
  --rtsp-url rtsp://192.168.88.91/live/test1
```

查看模拟器期望状态：

```sh
python3 case/platform/mock_platform.py status
```

停止该路任务：

```sh
python3 case/platform/mock_platform.py stop \
  --stream-id camera-01
```

停止全部模拟任务：

```sh
python3 case/platform/mock_platform.py stop-all
```

## 单路多事件

平台任务包含一路设备以及该设备需要判断的事件列表。使用多个
`--algorithm` 参数模拟同一路任务的多个事件：

```sh
python3 case/platform/mock_platform.py start \
  --stream-id camera-02 \
  --rtsp-url rtsp://192.168.88.91/live/test2 \
  --algorithm \
    hardhat-detection=/path/hardhat_detect_eic7700_1_2.pkg,0.55,2 \
  --algorithm \
    phone-detection=/path/phone_detect_eic7700_1_2.pkg,0.60,2
```

参数格式为：

```text
场景编码=pkg绝对路径[,置信度阈值[,告警等级]]
```

告警等级取值为 `1` 到 `4`。重复对相同 `stream-id` 执行 `start` 表示更新
该任务；执行 `stop` 只删除该路任务，不应影响其他任务。

## 状态检查

```sh
# 管理进程和 worker
pgrep -a pipeline_agent
pgrep -fc 'pipeline_agent --worker'

# 配置合并与 worker 增删
grep -aE \
  'received config|merged config|worker started|stopping worker|worker stopped|reconciled' \
  /tmp/pipeline-mock-agent.log | tail -n 100

# MMZ/VB
cat /proc/eswin/vb

# 解码、NPU、DSP
sudo /opt/eswin/bin/es_hw_watcher -i 2 -c 10
```

更完整的部署、告警、证据文件和压力测试步骤见 [run.md](run.md)。
