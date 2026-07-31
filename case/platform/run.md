# Pipeline 平台模式完整运行手册

本文说明 EIC7700 开发板上 Pipeline 平台模式的部署、真实平台启动、模拟平台
任务下发、状态检查、告警验证、资源检查和停止流程。

## 1. 运行模型

平台模式使用一个可执行文件：

```text
pipeline_agent
├── 管理进程：平台 IPC、任务状态、心跳、worker 生命周期、告警转发
└── worker 子进程：pipeline_agent --worker，运行一路任务的 Pipeline 图
```

平台每次下发的 `AgentConfig` 可以只包含一路任务的增量更新：

- `StreamConfig.enabled=true`：新增或更新该 `stream_id`；
- `StreamConfig.enabled=false`：停止并删除该 `stream_id`；
- 未出现在本次消息中的其他活动任务保持不变；
- 新增、更新和停止一路任务时，只协调受影响的 worker。

一个任务包含一路设备和多个 `AlgorithmConfig`。worker 隔离键是
`stream_id`，因此一个任务只有一个 RTSP、一个 VDEC 和一个 worker。worker
内部根据 `config/ModelGroups.yaml` 将事件聚合为模型分支；共享模型只推理
一次，确实不同的模型通过独立有界队列异步运行。

## 2. 开发板目录

默认部署位置：

```text
/home/ubuntu/workspace/test/pipeline
```

最小平台模式运行文件包括：

```text
bin/pipeline_agent
lib/
config/Event.yaml
config/EsTracker.yaml
config/ModelGroups.yaml
case/platform/launch_pipeline_agent.sh
case/platform/mock_platform.py
case/platform/run_real_platform.sh
case/platform/run_mock_platform_e2e.sh
```

模型 pkg 由平台配置传入绝对路径。Pipeline 直接解密并使用 pkg 内的
`.model`、量化表和运行参数，不再使用板端固定 `.model` 替换。

## 3. 首次运行检查

```sh
cd /home/ubuntu/workspace/test/pipeline

test -x bin/pipeline_agent && echo "pipeline_agent OK"
test -r config/Event.yaml && echo "Event.yaml OK"
test -r config/EsTracker.yaml && echo "EsTracker.yaml OK"
test -r config/ModelGroups.yaml && echo "ModelGroups.yaml OK"
```

真实平台模式还要检查平台服务和 UDS：

```sh
sudo systemctl is-active smart-guard-edge.service
test -S /opt/smart-guard/run/media-agent/media_agent.sock &&
  echo "platform socket OK"
test -w /opt/smart-guard/run/media-agent/media_agent.sock &&
  echo "platform socket writable"
```

检查模型 pkg：

```sh
pkg=/mnt/userdata/smart-guard-edge/recordings/default/ai-models/hardhat-detection/hardhat_detect_eic7700_1_2.pkg
test -r "$pkg" && sha256sum "$pkg"
```

首次部署或平台重建证据目录后，授予 `ubuntu` 写权限：

```sh
base=/mnt/userdata/smart-guard-edge/recordings/default/ai-events
sudo setfacl -R -m u:ubuntu:rwx "$base"
sudo find "$base" -type d -exec setfacl -m d:u:ubuntu:rwx {} +
```

## 4. 清理旧实例

真实平台和模拟平台不能同时启动两个 `pipeline_agent` 管理进程。

```sh
pkill -TERM -x pipeline_agent 2>/dev/null || true
sleep 25
pkill -KILL -x pipeline_agent 2>/dev/null || true
pkill -KILL -x espl_launch 2>/dev/null || true

pgrep -a pipeline_agent || echo "pipeline_agent stopped"
```

确认所有进程退出后再清理临时目录：

```sh
rm -rf /tmp/pipeline-agent /tmp/pipeline-mock-agent
rm -f \
  /tmp/pipeline-platform.log \
  /tmp/pipeline-mock-agent.log \
  /tmp/pipeline-mock-server.log \
  /tmp/pipeline-mock-platform.sock \
  /tmp/pipeline-mock-control.sock
```

## 5. 连接真实平台

推荐使用完整启动脚本。首次启动或明确重启：

```sh
cd /home/ubuntu/workspace/test/pipeline

./case/platform/run_real_platform.sh --restart
```

脚本自动执行：

- 检查二进制、动态库和配置；
- 检查 `smart-guard-edge.service`；
- 检查真实平台 socket 是否存在且可写；
- 优雅停止旧管理进程和 worker；
- 清理运行目录；
- 后台脱离 SSH 终端；
- 等待平台连接和告警 relay socket；
- 写入 PID 文件并打印进程状态。

只做前置检查：

```sh
./case/platform/run_real_platform.sh --dry-run
```

前台调试：

```sh
./case/platform/run_real_platform.sh \
  --restart \
  --foreground
```

平台重新创建证据目录后，同时修复当前用户 ACL：

```sh
./case/platform/run_real_platform.sh \
  --restart \
  --fix-evidence-permissions
```

查看全部参数：

```sh
./case/platform/run_real_platform.sh --help
```

底层 `launch_pipeline_agent.sh` 仍可用于调试，但不负责停止旧进程、清理、服务检查
和连接超时处理。

完整启动脚本默认使用：

| 环境变量 | 默认值 |
| --- | --- |
| `PIPELINE_PLATFORM_SOCKET` | `/opt/smart-guard/run/media-agent/media_agent.sock` |
| `PIPELINE_AGENT_RUNTIME_DIR` | `/tmp/pipeline-agent` |
| `PIPELINE_ALARM_RELAY_SOCKET` | `$runtime_dir/alarm.sock` |
| `PIPELINE_AGENT_ID` | `agent_001` |
| `PIPELINE_CONFIG_DEBOUNCE_MS` | `2000` |
| `PIPELINE_CONFIG_MAX_WAIT_MS` | `10000` |
| `PIPELINE_WORKER_STOP_TIMEOUT_MS` | `20000` |
| `PIPELINE_VDEC_POOL_SIZE` | `2` |
| `PIPELINE_MUX_POOL_SIZE` | `8` |
| `PIPELINE_PREPROCESS_POOL_SIZE` | `12` |
| `PIPELINE_INFER_OUTPUT_POOL_SIZE` | `8` |

检查连接和任务：

```sh
pgrep -a pipeline_agent
ls -l /tmp/pipeline-agent/alarm.sock

grep -aE \
  'SocketSender|received config|merged config|worker started|reconciled' \
  /tmp/pipeline-platform.log | tail -n 100
```

正常日志顺序：

```text
SocketSender connected
received config
merged config_id=... updates=... active_streams=...
worker started
reconciled streams=... worker_groups=...
```

## 6. 启动模拟平台

推荐使用全流程验证脚本：

```sh
cd /home/ubuntu/workspace/test/pipeline

./case/platform/run_mock_platform_e2e.sh
```

默认流程：

1. 记录测试前真实平台状态和 MMZ；
2. 停止现有 Pipeline；
3. 启动模拟 UDS 和 Pipeline 管理进程；
4. 逐路启动 3 个任务；
5. 停止并恢复第 2 路，比较其他 worker PID；
6. 为第 1 路增加并移除第二事件，确认 worker 总数不变、其他路 PID 不变；
7. 检查截图、TS、心跳、ACK 和告警；
8. 采集 VDEC/NPU/DSP 和进程状态；
9. `stop-all` 并检查 MMZ 恢复；
10. 关闭模拟环境，并按测试前状态恢复真实平台。

13 路回归：

```sh
./case/platform/run_mock_platform_e2e.sh \
  --channels 13 \
  --churn-index 7 \
  --multi-event-index 1 \
  --task-interval 3 \
  --evidence-timeout 180
```

使用独立 phone pkg 验证第二事件：

```sh
./case/platform/run_mock_platform_e2e.sh \
  --extra-scenario phone-detection \
  --extra-package /path/phone_detect_eic7700_1_2.pkg
```

验证共享通用目标模型只创建一个推理分支：

```sh
./case/platform/run_mock_platform_e2e.sh \
  --channels 1 \
  --skip-churn \
  --scenario area-intrusion \
  --package /mnt/userdata/smart-guard-edge/recordings/default/ai-models/area-intrusion/area-intrusion_eic7700_1_2.pkg \
  --extra-scenario area-loitering \
  --extra-package /mnt/userdata/smart-guard-edge/recordings/default/ai-models/area-loitering/area-loitering_eic7700_1_1.pkg
```

管理日志应出现 `model_groups=1` 和
`groups=[general-object-detection:2]`。

只测试配置和 worker 生命周期：

```sh
./case/platform/run_mock_platform_e2e.sh \
  --evidence-mode off \
  --skip-hardware \
  --mmz-mode observe
```

验证结果默认保存在：

```text
/tmp/pipeline_mock_validation_<时间>_<PID>/
```

其中包含管理进程日志、模拟平台日志、每阶段 worker 映射、MMZ 快照、硬件
统计、证据检查和 `summary.env`。

查看全部参数：

```sh
./case/platform/run_mock_platform_e2e.sh --help
```

以下步骤用于手工拆分调试。

模拟器使用两个 UDS：

- `/tmp/pipeline-mock-platform.sock`：供 `pipeline_agent` 连接；
- `/tmp/pipeline-mock-control.sock`：供 `start/stop/status` 命令连接。

先启动模拟平台服务端：

```sh
cd /home/ubuntu/workspace/test/pipeline

setsid -f python3 case/platform/mock_platform.py serve \
  >/tmp/pipeline-mock-server.log 2>&1 </dev/null

sleep 1
test -S /tmp/pipeline-mock-platform.sock
test -S /tmp/pipeline-mock-control.sock
```

再启动 Pipeline 管理进程：

```sh
PIPELINE_PLATFORM_SOCKET=/tmp/pipeline-mock-platform.sock \
PIPELINE_AGENT_RUNTIME_DIR=/tmp/pipeline-mock-agent \
PIPELINE_ALARM_RELAY_SOCKET=/tmp/pipeline-mock-agent/alarm.sock \
setsid -f ./case/platform/launch_pipeline_agent.sh \
  >/tmp/pipeline-mock-agent.log 2>&1 </dev/null

sleep 3
python3 case/platform/mock_platform.py status
```

此时 `pipeline_connected` 应为 `true`，`active_count` 应为 `0`。

## 7. 模拟启动任务

使用默认安全帽 pkg 启动一路：

```sh
python3 case/platform/mock_platform.py start \
  --stream-id camera-01 \
  --rtsp-url rtsp://192.168.88.91/live/test1
```

显式指定事件和 pkg：

```sh
python3 case/platform/mock_platform.py start \
  --stream-id camera-02 \
  --rtsp-url rtsp://192.168.88.91/live/test2 \
  --algorithm \
    hardhat-detection=/mnt/userdata/smart-guard-edge/recordings/default/ai-models/hardhat-detection/hardhat_detect_eic7700_1_2.pkg,0.55,2
```

同一路任务包含多个事件：

```sh
python3 case/platform/mock_platform.py start \
  --stream-id camera-03 \
  --rtsp-url rtsp://192.168.88.91/live/test3 \
  --algorithm \
    hardhat-detection=/path/hardhat_detect_eic7700_1_2.pkg,0.55,2 \
  --algorithm \
    phone-detection=/path/phone_detect_eic7700_1_2.pkg,0.60,2
```

连续增加 13 路：

```sh
for index in $(seq 1 13); do
  python3 case/platform/mock_platform.py start \
    --stream-id "$(printf 'camera-%02d' "$index")" \
    --rtsp-url "rtsp://192.168.88.91/live/test${index}"
  sleep 3
done
```

模拟器返回的 `active_count` 是已成功发送的期望任务数。实际 worker 状态仍要
以 Pipeline 日志和进程为准。

## 8. 模拟更新和停止任务

相同 `stream-id` 再次执行 `start` 表示更新该路 URL、事件或模型。该路 worker
可能重建，其他路 PID 不应变化。

停止一路：

```sh
python3 case/platform/mock_platform.py stop \
  --stream-id camera-07
```

恢复该路：

```sh
python3 case/platform/mock_platform.py start \
  --stream-id camera-07 \
  --rtsp-url rtsp://192.168.88.91/live/test7
```

停止全部：

```sh
python3 case/platform/mock_platform.py stop-all
```

查看模拟器期望状态：

```sh
python3 case/platform/mock_platform.py status
```

停止模拟平台服务：

```sh
python3 case/platform/mock_platform.py shutdown
```

## 9. 检查增量任务行为

```sh
grep -aE \
  'received config|merged config|worker started|stopping worker|worker stopped|reconciled' \
  /tmp/pipeline-mock-agent.log | tail -n 200
```

预期行为：

1. 每次 `start` 后 `active_streams` 增加或保持不变；
2. 新增一路只出现该路的 `worker started`；
3. 停止一路只出现该路的 `stopping worker` 和 `worker stopped`；
4. 其他路的 worker PID、VDEC group 和解码累计值保持连续；
5. 全部停止后管理进程仍运行，worker 数为零。

进程检查：

```sh
pgrep -a pipeline_agent
pgrep -fc 'pipeline_agent --worker'
```

保存停止前的 PID，再比较其他任务是否重启：

```sh
pgrep -af 'pipeline_agent --worker' > /tmp/workers.before

python3 case/platform/mock_platform.py stop --stream-id camera-07
sleep 25

pgrep -af 'pipeline_agent --worker' > /tmp/workers.after
diff -u /tmp/workers.before /tmp/workers.after || true
```

## 10. 告警和证据验证

Pipeline 侧：

```sh
grep -aE \
  'alarm queued|alarm queue failed|snapshot saved|record saved|ERROR|FATAL' \
  /tmp/pipeline-platform.log | tail -n 100
```

真实平台后端：

```sh
sudo journalctl -fu smart-guard-edge.service |
grep --line-buffered -E \
  'media-agent alarm received|alarm persisted|event published'
```

最近生成的证据文件：

```sh
find /mnt/userdata/smart-guard-edge/recordings/default/ai-events \
  -type f \( -name '*.jpg' -o -name '*.ts' \) -mmin -5 \
  -printf '%TY-%Tm-%Td %TH:%TM:%TS %s %p\n' |
sort | tail -n 50
```

模拟平台不实现平台告警持久化。它会读取 Pipeline 发来的 Envelope，并在
`/tmp/pipeline-mock-server.log` 中记录消息类型、序号和大小；截图与录像仍由
worker 写入任务指定目录。

## 11. MMZ、VDEC、NPU 和 CPU 检查

查看 VB/MMZ：

```sh
cat /proc/eswin/vb
```

关注：

- `free mem size` 是否在任务稳定后保持稳定；
- 新增一路时的下降量；
- 停止一路后是否回升；
- 全部停止后 Pipeline 创建的 pool 是否完全释放。

硬件统计：

```sh
sudo /opt/eswin/bin/es_hw_watcher -i 2 -c 15
```

解码累计值：

```sh
tr -d '\000' </proc/esmap/dec |
sed -n '/GRP STATUS/,/CHN STATUS/p'
```

20 秒 CPU 平均值：

```sh
pids=$(pgrep -x pipeline_agent | paste -sd, -)
pidstat -u -p "$pids" 1 20
```

生产配置 `EsPreProcess.yaml` 使用 `interval: [3, 1]`。25 FPS 输入时每路约
向 NPU 提交 `25 / 3 = 8.33 FPS`，13 路约 `108.3 FPS`。VDEC 仍解码全部
输入帧，因此 13 路 25 FPS 的名义解码总帧率是 325 FPS。

短窗口内 `es_hw_watcher` 的 Usage 和 Framerate 会因异步批次、RTSP 到帧抖动
及 2 秒采样窗口而波动，应使用 30 秒以上累计值判断吞吐。

## 12. 错误检查

```sh
grep -aEi \
  'Cannot allocate memory|create pool failed|pipeline start failed|worker exited|failed at|DetectionOut|FATAL' \
  /tmp/pipeline-platform.log /tmp/pipeline-mock-agent.log 2>/dev/null
```

常见问题：

| 现象 | 检查 |
| --- | --- |
| `platform socket not found` | 先启动真实平台服务或 `mock_platform.py serve` |
| socket 不可写 | 检查属主、组和 ACL |
| pkg 解密失败 | 检查 pkg 路径、权限、密钥和 manifest |
| 后处理启动失败 | 检查 pkg 是否为 3 个 S16 DetectionOut 输出 |
| MMZ 持续下降 | 检查是否反复重启 worker、pool 是否释放、是否重复启动管理进程 |
| 只启动一路 | 检查每次消息是否使用不同 `stream_id`，以及是否被当作全量覆盖 |

## 13. 停止与清理

真实平台模式：

```sh
pkill -TERM -x pipeline_agent
sleep 25
pgrep -a pipeline_agent || echo "pipeline stopped"
cat /proc/eswin/vb
```

模拟平台模式：

```sh
python3 case/platform/mock_platform.py stop-all
sleep 25
pkill -TERM -x pipeline_agent 2>/dev/null || true
python3 case/platform/mock_platform.py shutdown 2>/dev/null || true

rm -rf /tmp/pipeline-mock-agent
rm -f \
  /tmp/pipeline-mock-platform.sock \
  /tmp/pipeline-mock-control.sock
```

`smart-guard-edge.service` 是平台后端服务，不应随 Pipeline 一起停止。
