# Pipeline 平台模式运行说明

本文说明 EIC7700 开发板上 pipeline 平台模式的首次准备、启动、平台任务
启停验证、告警验证和程序停止过程。

平台模式只需要启动一个 `pipeline_agent` 可执行文件。`pipeline_agent` 同时
负责平台 IPC、任务状态管理和 worker 生命周期；worker 通过
`pipeline_agent --worker` 子进程运行官方 pipeline 插件。`espl_launch`
仅用于兼容原有独立 case，不是平台模式的必需文件。

## 1. 运行目录

开发板地址和运行目录：

```text
IP: 192.168.88.127
用户: ubuntu
目录: /home/ubuntu/workspace/test/pipeline
```

登录并进入目录：

```sh
ssh ubuntu@192.168.88.127
cd /home/ubuntu/workspace/test/pipeline
```

## 2. 首次运行检查

确认平台后端和 Unix socket 正常：

```sh
sudo systemctl is-active smart-guard-edge.service
test -S /opt/smart-guard/run/media-agent/media_agent.sock &&
    echo "platform socket OK"
```

确认平台下发的安全帽 pkg 可读：

```sh
test -r \
  /mnt/userdata/smart-guard-edge/recordings/default/ai-models/hardhat-detection/hardhat_detect_eic7700_1_2.pkg &&
    echo "hardhat pkg OK"
```

首次部署或平台重新创建证据目录后，授予 `ubuntu` 用户写权限：

```sh
base=/mnt/userdata/smart-guard-edge/recordings/default/ai-events
sudo setfacl -R -m u:ubuntu:rwx "$base"
sudo find "$base" -type d -exec setfacl -m d:u:ubuntu:rwx {} +
```

## 3. 清理旧实例

启动前必须确保没有旧的 `media_agent`、`pipeline_agent` 或 worker。不要在
已有 `pipeline_agent` 运行时重复执行启动脚本。

```sh
sudo pkill -TERM -x media_agent 2>/dev/null || true
pkill -TERM -x pipeline_agent 2>/dev/null || true
pkill -TERM -x espl_launch 2>/dev/null || true
sleep 3

pkill -KILL -x pipeline_agent 2>/dev/null || true
pkill -KILL -x espl_launch 2>/dev/null || true

pgrep -a media_agent || echo "media_agent stopped"
pgrep -a pipeline_agent || echo "pipeline_agent stopped"
pgrep -a espl_launch || echo "legacy espl_launch stopped"
```

所有进程停止后才能清理运行目录：

```sh
rm -rf /tmp/pipeline-agent
mkdir -p /tmp/pipeline-agent
rm -f /tmp/pipeline-platform.log
```

## 4. 启动程序

推荐使用后台方式：

```sh
cd /home/ubuntu/workspace/test/pipeline
setsid -f ./case/platform/platform_agent.sh \
    >/tmp/pipeline-platform.log 2>&1 </dev/null
sleep 5
```

需要直接观察完整控制台时，可改用前台方式：

```sh
cd /home/ubuntu/workspace/test/pipeline
./case/platform/platform_agent.sh
```

前台方式必须保持 SSH 终端打开。按 `Ctrl+C` 会同时停止管理进程及其 worker。
启动脚本带有单实例锁，重复启动会返回 `pipeline_agent is already running`。

## 5. 检查平台连接

管理进程启动后应立即存在，worker 是否存在由平台上的活动任务决定：

```sh
pgrep -a pipeline_agent
ls -l /tmp/pipeline-agent/alarm.sock
```

查看平台连接及任务配置：

```sh
grep -aE \
  'SocketSender|received config|merged config|worker started|reconciled' \
  /tmp/pipeline-platform.log | tail -n 100
```

正常日志应依次包含：

```text
SocketSender connected
received config
merged config_id=... updates=... active_streams=...
worker started
reconciled streams=... worker_groups=...
```

## 6. 验证平台逐任务启动

每个任务消息只更新其中的一路设备。启动任务 A 后：

```sh
grep -aE 'received config|merged config|reconciled' \
    /tmp/pipeline-platform.log | tail -n 20
```

应看到 `active_streams=1`。再启动任务 B，应看到 `active_streams=2`，任务 A
不会从活动任务表中删除。连续启动更多任务时，该数字应逐步增加。

每一路设备及其事件使用独立 worker。新增任务只创建该路 worker，已有 worker
的 PID、VDEC group 和推理上下文保持不变。13 路任务正常应有一个管理进程和
13 个 worker：

```sh
pgrep -fc 'pipeline_agent --worker'
grep -a 'worker started' /tmp/pipeline-platform.log | tail -n 20
```

## 7. 验证平台逐任务停止

在平台停止任务 B 后，日志中的 `active_streams` 应减一，任务 A 仍保留：

```sh
grep -aE 'received config|merged config|reconciled|worker stopped' \
    /tmp/pipeline-platform.log | tail -n 30
```

停止全部任务后，`pipeline_agent` 应继续运行，但不再有 worker：

```sh
pgrep -a pipeline_agent
pgrep -f 'pipeline_agent --worker' ||
    echo "all platform workers stopped"
```

平台再次启动任意任务时，无需重启 `pipeline_agent`。

## 8. 验证告警和证据文件

Pipeline 侧日志：

```sh
grep -aE \
  'alarm queued|alarm queue failed|snapshot saved|record saved|ERROR|FATAL' \
  /tmp/pipeline-platform.log | tail -n 100
```

正常情况必须出现 `alarm queued`，不能持续出现 `alarm queue failed`。

平台后端接收、入库和发布日志：

```sh
sudo journalctl -fu smart-guard-edge.service |
grep --line-buffered -E \
  'media-agent alarm received|alarm persisted|event published'
```

查看最近生成的原分辨率截图和录像：

```sh
find /mnt/userdata/smart-guard-edge/recordings/default/ai-events \
  -type f \( -name '*.jpg' -o -name '*.ts' \) -mmin -5 \
  -printf '%TY-%Tm-%Td %TH:%TM:%TS %s %p\n' |
  sort | tail -n 50
```

检查截图分辨率：

```sh
latest_jpg=$(find \
  /mnt/userdata/smart-guard-edge/recordings/default/ai-events \
  -type f -name '*.jpg' -mmin -5 | head -n 1)
file "$latest_jpg"
```

## 9. 停止程序

先停止管理进程，它会通知所有 worker 退出：

```sh
pkill -TERM -x pipeline_agent
sleep 25
```

确认并清理异常残留：

```sh
pgrep -a pipeline_agent || echo "pipeline stopped"
pkill -KILL -x pipeline_agent 2>/dev/null || true
pkill -KILL -x espl_launch 2>/dev/null || true
rm -rf /tmp/pipeline-agent
```

`smart-guard-edge.service` 是平台后端服务，不应随 pipeline 一起停止。

## 10. 连续下发任务与 MMZ 回归测试

先停止真实平台模式，启动本地平台模拟器。模拟器会以 250 ms 间隔连续增加
13 路 1080P RTSP 任务：

```sh
cd /home/ubuntu/workspace/test/pipeline
pkill -TERM -x pipeline_agent 2>/dev/null || true
sleep 25

rm -rf /tmp/mmz-agent-test
rm -f /tmp/mmz-platform.sock /tmp/mmz-server.log /tmp/mmz-agent.log

python3 scripts/mmz_increment_server.py \
  --socket /tmp/mmz-platform.sock \
  --count 13 --interval 0.25 \
  --churn-index 7 --churn-delay 20 --hold 3600 \
  >/tmp/mmz-server.log 2>&1 &

PIPELINE_AGENT_RUNTIME_DIR=/tmp/mmz-agent-test \
PIPELINE_PLATFORM_SOCKET=/tmp/mmz-platform.sock \
PIPELINE_ALARM_RELAY_SOCKET=/tmp/mmz-agent-test/alarm.sock \
PERF_STATIC_FLAG=1 PIPELINE_WORKER_PERF_STATIC_FLAG=1 \
PL_LOG_LEVEL=2 PIPELINE_WORKER_LOG_LEVEL=1 \
./case/platform/platform_agent.sh >/tmp/mmz-agent.log 2>&1 &
```

确认 13 路均有独立 worker，并检查第 7 路停止、恢复和其他路的连续解码：

```sh
grep -aE 'worker started|stopping worker|worker stopped|reconciled' \
  /tmp/mmz-agent.log
grep -a 'getFramePerformance' /tmp/mmz-agent.log | tail -n 26
tr -d '\000' </proc/esmap/dec |
  sed -n '/GRP STATUS/,/CHN STATUS/p'
grep -aEi \
  'Cannot allocate memory|create pool failed|pipeline start failed|worker exited|failed at' \
  /tmp/mmz-agent.log
```

正常结果是初始 13 条 `worker started`。第 7 路停止时只出现一条
`stopping worker ... stream=mmz-test-07` 和对应的 `worker stopped`，
worker 数从 13 变为 12；恢复时只为 `mmz-test-07` 新增一条
`worker started`。其他 VDEC group 的 `DecodeFrmNum` 在此期间应持续增长，
错误检查没有输出。

运行中查看 MMZ，结束后确认资源完全回收：

```sh
cat /proc/eswin/vb

pkill -TERM -x pipeline_agent
sleep 25
cat /proc/eswin/vb
pgrep -a pipeline_agent || echo "pipeline stopped"
```

停止后 `/proc/eswin/vb` 的 `free mem size` 应等于 `total size`，且
`POOL CONFIG` 下不应残留 pipeline 创建的池。完成模拟测试后，按第 4 节命令
重新启动真实平台模式。

## 11. CPU、VDEC 与 NPU 性能检查

生产模式默认关闭逐元素性能落盘，worker 只输出 WARN/ERROR。需要采集
10 秒一次的 Pipeline 性能统计时，在启动前显式设置：

```sh
PERF_STATIC_FLAG=1 PIPELINE_WORKER_PERF_STATIC_FLAG=1 \
PL_LOG_LEVEL=2 PIPELINE_WORKER_LOG_LEVEL=1 \
setsid -f ./case/platform/platform_agent.sh \
  >/tmp/pipeline-platform.log 2>&1 </dev/null
```

`EsPreProcess.yaml` 当前使用 `interval: [3, 1]`。预处理按帧序号计算
`floor(frame_index / 3)`，仅当结果相对上一帧发生变化时放行，所以每路放行
第 0、3、6、9... 帧。25 FPS 输入时，每路 NPU 输入约为 `25 / 3 = 8.33 FPS`；
13 路约为 `108.3 FPS`。VDEC 仍解码所有帧，13 路名义总帧率为 325 FPS。

不要用 `top` 的单次刷新判断 worker 的长期 CPU。使用 20 秒平均值：

```sh
pids=$(pgrep -x pipeline_agent | paste -sd, -)
pidstat -u -p "$pids" 1 20
```

`es_hw_watcher` 默认使用 2 秒窗口，异步任务集中提交时读数会明显跳动。VDEC
是否丢帧应优先使用 `/proc/esmap/dec` 的累计 `DecodeFrmNum` 做长窗口差值：

```sh
vdec_total()
{
  tr -d '\000' </proc/esmap/dec |
  awk '/GRP STATUS/{in_grp=1;next}
       /CHN STATUS/{in_grp=0}
       in_grp && $1 ~ /^[0-9]+$/ {sum += $7}
       END{print sum+0}'
}

t0=$(date +%s); f0=$(vdec_total)
sleep 30
t1=$(date +%s); f1=$(vdec_total)
awk -v f0="$f0" -v f1="$f1" -v t0="$t0" -v t1="$t1" \
  'BEGIN {printf "aggregate VDEC: %.2f fps\n", (f1-f0)/(t1-t0)}'

sudo /opt/eswin/bin/es_hw_watcher -i 2 -c 15
```

长时间运行时应轮转或定期归档 `/tmp/pipeline-platform.log`。只有定位 Pipeline
内部问题时才临时设置 `PIPELINE_WORKER_LOG_LEVEL=3`，避免逐帧 DEBUG 日志干扰
CPU、I/O 和调度。
