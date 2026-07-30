#!/usr/bin/env bash

set -Eeuo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
pipeline_root=$(cd -- "$script_dir/../.." && pwd)
mock="$script_dir/mock_platform.py"
launcher="$script_dir/launch_pipeline_agent.sh"
real_starter="$script_dir/run_real_platform.sh"
agent_binary="$pipeline_root/bin/pipeline_agent"

channels=3
start_index=1
stream_prefix=mock
rtsp_base=rtsp://192.168.88.91/live
rtsp_name_prefix=test
package_path=/mnt/userdata/smart-guard-edge/recordings/default/ai-models/hardhat-detection/hardhat_detect_eic7700_1_2.pkg
scenario=hardhat-detection
threshold=0.5
alarm_level=2
record_duration=10
dedup_interval=30
task_interval=4
settle_seconds=15
operation_wait=3
startup_timeout=20
worker_timeout=45
stop_timeout=30
evidence_timeout=120
evidence_mode=all
expected_width=1920
expected_height=1080
churn_index=2
multi_event_index=1
extra_scenario=phone-detection
extra_package=
hardware_seconds=8
check_hardware=1
mmz_mode=strict
restore_real=auto
real_socket=/opt/smart-guard/run/media-agent/media_agent.sock
result_dir=
dry_run=0

platform_socket=
control_socket=
runtime_dir=
evidence_dir=
server_log=
agent_log=
summary_file=
server_pid=
agent_pid=
previous_real=0
cleanup_done=0
config_commands=0

usage() {
    cat <<'EOF'
用法：
  run_mock_platform_e2e.sh [选项]

任务输入：
      --channels N              任务路数，默认 3
      --start-index N           RTSP 和流编号起点，默认 1
      --stream-prefix NAME      stream_id 前缀，默认 mock
      --rtsp-base URL           RTSP 基础地址
      --rtsp-name-prefix NAME   RTSP 资源名前缀，默认 test
      --package PATH            主模型 pkg
      --scenario CODE           主事件编码，默认 hardhat-detection
      --threshold FLOAT         主事件阈值，默认 0.5
      --alarm-level N           告警等级 1-4，默认 2
      --record-duration N       录像秒数，默认 10
      --dedup-interval N        告警去重秒数，默认 30

流程控制：
      --task-interval N         相邻任务下发间隔，默认 4 秒
      --settle-seconds N        全部启动后的稳定等待，默认 15 秒
      --operation-wait N        启停操作后附加等待，默认 3 秒
      --startup-timeout N       模拟平台连接超时，默认 20 秒
      --worker-timeout N        worker 数量调和超时，默认 45 秒
      --stop-timeout N          进程停止超时，默认 30 秒

单路停复测试：
      --churn-index N           停止/恢复第 N 路，默认 2
      --skip-churn              跳过单路停止/恢复

单路多事件测试：
      --multi-event-index N     为第 N 路增加第二事件，默认 1
      --extra-scenario CODE     第二事件编码，默认 phone-detection
      --extra-package PATH      第二事件 pkg，默认复用主 pkg
      --skip-multi-event        跳过多事件增删

证据与硬件：
      --evidence-mode MODE      all | any | off，默认 all
      --evidence-timeout N      证据等待时间，默认 120 秒
      --expected-size WxH       期望截图尺寸，默认 1920x1080
      --hardware-seconds N      硬件采样时间，默认 8 秒
      --skip-hardware           不运行 es_hw_watcher
      --mmz-mode MODE           strict | observe | off，默认 strict

环境和收尾：
      --result-dir DIR          结果目录，默认 /tmp 下带时间戳目录
      --restore-real MODE       auto | always | never，默认 auto
      --real-socket PATH        恢复的真实平台 socket
      --dry-run                 只检查参数和依赖

说明：
  auto 仅在测试前检测到真实平台管理进程时恢复。
  strict 要求 stop-all 后 MMZ free mem 恢复到测试前基线。
  all 要求每一路都生成 JPG 和 TS；any 只要求至少一路。
EOF
}

log() {
    printf '[%s] %s\n' "$(date '+%F %T')" "$*"
}

die() {
    log "失败：$*" >&2
    exit 1
}

is_uint() {
    [[ $1 =~ ^[0-9]+$ ]]
}

is_number() {
    [[ $1 =~ ^[0-9]+([.][0-9]+)?$ ]]
}

stream_id_for_position() {
    local position=$1
    printf '%s-%02d' "$stream_prefix" "$((start_index + position - 1))"
}

rtsp_url_for_position() {
    local position=$1
    printf '%s/%s%d' "${rtsp_base%/}" "$rtsp_name_prefix" \
        "$((start_index + position - 1))"
}

worker_count() {
    pgrep -fc "$agent_binary --worker" 2>/dev/null || true
}

manager_count() {
    local pid cmdline count=0
    for pid in $(pgrep -x pipeline_agent 2>/dev/null || true); do
        [[ -r /proc/$pid/cmdline ]] || continue
        cmdline=$(tr '\0' ' ' <"/proc/$pid/cmdline")
        if [[ $cmdline == *"--platform-socket $platform_socket"* ]] &&
           [[ $cmdline != *" --worker "* ]]; then
            count=$((count + 1))
        fi
    done
    echo "$count"
}

worker_map() {
    local pid config_dir stream worker_scenario
    for pid in $(pgrep -f "$agent_binary --worker" 2>/dev/null || true); do
        [[ -r /proc/$pid/cmdline ]] || continue
        config_dir=$(
            tr '\0' '\n' <"/proc/$pid/cmdline" |
                awk '$0=="config_path" {getline; print; exit}'
        )
        [[ -n $config_dir ]] || continue
        stream=$(
            awk '/^stream-id:/ {print $2; exit}' \
                "${config_dir%/}/EsAvDemux_1.yaml"
        )
        worker_scenario=$(
            awk '/^scenario-code:/ {print $2; exit}' \
                "${config_dir%/}/EsEvent.yaml"
        )
        printf '%s %s %s %s\n' \
            "$stream" "$worker_scenario" "$pid" "$config_dir"
    done | sort
}

wait_for_worker_count() {
    local expected=$1 elapsed=0 actual
    while (( elapsed < worker_timeout )); do
        actual=$(worker_count)
        if [[ $actual -eq $expected ]]; then
            return 0
        fi
        if [[ -n $agent_pid ]] && ! kill -0 "$agent_pid" 2>/dev/null; then
            die "管理进程提前退出"
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    worker_map >&2 || true
    die "等待 worker 数量超时：期望 $expected，实际 $(worker_count)"
}

wait_process_exit() {
    local pid=$1 timeout=$2 elapsed=0
    [[ -n $pid ]] || return 0
    while kill -0 "$pid" 2>/dev/null && (( elapsed < timeout )); do
        sleep 1
        elapsed=$((elapsed + 1))
    done
    ! kill -0 "$pid" 2>/dev/null
}

read_vb() {
    if [[ -r /proc/eswin/vb ]]; then
        cat /proc/eswin/vb
    else
        sudo -n cat /proc/eswin/vb
    fi
}

vb_free() {
    sed -n 's/.*free mem size(\(0x[0-9a-fA-F]*\)).*/\1/p' | head -n 1
}

capture_vb() {
    local name=$1
    [[ $mmz_mode != off ]] || return 0
    read_vb >"$result_dir/vb.$name.txt"
}

stop_all_pipeline_processes() {
    local pids elapsed=0
    pids=$(pgrep -x pipeline_agent 2>/dev/null || true)
    [[ -n $pids ]] || return 0
    kill -TERM $pids 2>/dev/null || true
    while [[ -n $(pgrep -x pipeline_agent 2>/dev/null || true) ]] &&
          (( elapsed < stop_timeout )); do
        sleep 1
        elapsed=$((elapsed + 1))
    done
    pids=$(pgrep -x pipeline_agent 2>/dev/null || true)
    if [[ -n $pids ]]; then
        kill -KILL $pids 2>/dev/null || true
        sleep 1
    fi
    [[ -z $(pgrep -x pipeline_agent 2>/dev/null || true) ]] ||
        die "无法停止已有 pipeline_agent"
}

detect_previous_real() {
    local pid cmdline
    previous_real=0
    for pid in $(pgrep -x pipeline_agent 2>/dev/null || true); do
        [[ -r /proc/$pid/cmdline ]] || continue
        cmdline=$(tr '\0' ' ' <"/proc/$pid/cmdline")
        if [[ $cmdline == *"--platform-socket $real_socket"* ]] &&
           [[ $cmdline != *" --worker "* ]]; then
            previous_real=1
            return
        fi
    done
}

mock_command() {
    python3 "$mock" "$@" --control-socket "$control_socket"
}

start_stream() {
    local position=$1 stream url
    stream=$(stream_id_for_position "$position")
    url=$(rtsp_url_for_position "$position")
    mock_command start \
        --stream-id "$stream" \
        --rtsp-url "$url" \
        --snapshot-dir "$evidence_dir/$stream/snapshots" \
        --record-dir "$evidence_dir/$stream/records" \
        --record-duration "$record_duration" \
        --dedup-interval "$dedup_interval" \
        --algorithm "$scenario=$package_path,$threshold,$alarm_level"
    config_commands=$((config_commands + 1))
}

stop_stream() {
    local position=$1 stream
    stream=$(stream_id_for_position "$position")
    mock_command stop --stream-id "$stream"
    config_commands=$((config_commands + 1))
}

start_stream_multi_event() {
    local position=$1 stream url
    stream=$(stream_id_for_position "$position")
    url=$(rtsp_url_for_position "$position")
    mock_command start \
        --stream-id "$stream" \
        --rtsp-url "$url" \
        --snapshot-dir "$evidence_dir/$stream/snapshots" \
        --record-dir "$evidence_dir/$stream/records" \
        --record-duration "$record_duration" \
        --dedup-interval "$dedup_interval" \
        --algorithm "$scenario=$package_path,$threshold,$alarm_level" \
        --algorithm "$extra_scenario=$extra_package,$threshold,$alarm_level"
    config_commands=$((config_commands + 1))
}

pid_from_map() {
    local map_file=$1 stream=$2 event=$3
    awk -v stream="$stream" -v event="$event" \
        '$1==stream && $2==event {print $3; exit}' "$map_file"
}

assert_base_workers_unchanged() {
    local before=$1 after=$2 skip_position=${3:-0}
    local position stream old_pid new_pid
    for ((position = 1; position <= channels; ++position)); do
        [[ $position -eq $skip_position ]] && continue
        stream=$(stream_id_for_position "$position")
        old_pid=$(pid_from_map "$before" "$stream" "$scenario")
        new_pid=$(pid_from_map "$after" "$stream" "$scenario")
        [[ -n $old_pid && $old_pid == "$new_pid" ]] ||
            die "worker 意外变化：stream=$stream before=$old_pid after=$new_pid"
    done
}

wait_for_evidence() {
    local elapsed=0 position stream jpg ts ready
    [[ $evidence_mode != off ]] || return 0
    while (( elapsed < evidence_timeout )); do
        ready=0
        if [[ $evidence_mode == any ]]; then
            jpg=$(find "$evidence_dir" -type f -name '*.jpg' -print -quit 2>/dev/null || true)
            ts=$(find "$evidence_dir" -type f -name '*.ts' -print -quit 2>/dev/null || true)
            [[ -n $jpg && -n $ts ]] && return 0
        else
            ready=1
            for ((position = 1; position <= channels; ++position)); do
                stream=$(stream_id_for_position "$position")
                jpg=$(find "$evidence_dir/$stream" -type f -name '*.jpg' -print -quit 2>/dev/null || true)
                ts=$(find "$evidence_dir/$stream" -type f -name '*.ts' -print -quit 2>/dev/null || true)
                if [[ -z $jpg || -z $ts ]]; then
                    ready=0
                    break
                fi
            done
            [[ $ready -eq 1 ]] && return 0
        fi
        sleep 2
        elapsed=$((elapsed + 2))
    done
    die "等待证据文件超时，模式=$evidence_mode"
}

validate_evidence() {
    local position stream jpg ts
    [[ $evidence_mode != off ]] || return 0
    : >"$result_dir/evidence-check.log"
    for ((position = 1; position <= channels; ++position)); do
        stream=$(stream_id_for_position "$position")
        jpg=$(find "$evidence_dir/$stream" -type f -name '*.jpg' | sort | tail -n 1)
        ts=$(find "$evidence_dir/$stream" -type f -name '*.ts' | sort | tail -n 1)
        if [[ $evidence_mode == any && ( -z $jpg || -z $ts ) ]]; then
            continue
        fi
        [[ -n $jpg && -n $ts ]] || die "$stream 缺少截图或录像"
        {
            echo "[$stream]"
            file "$jpg"
            ffprobe -v error -select_streams v:0 \
                -show_entries stream=codec_name,width,height \
                -show_entries format=duration,size \
                -of default=noprint_wrappers=1 "$ts"
        } >>"$result_dir/evidence-check.log"
        if [[ $expected_width -gt 0 && $expected_height -gt 0 ]]; then
            file "$jpg" |
                grep -q "${expected_width}x${expected_height}" ||
                die "$stream 截图尺寸不符合 ${expected_width}x${expected_height}"
        fi
    done
}

capture_hardware() {
    [[ $check_hardware -eq 1 ]] || return 0
    tr -d '\000' </proc/esmap/dec >"$result_dir/esmap-dec.txt" 2>/dev/null || true
    {
        ps -eo pid,ppid,stat,psr,%cpu,%mem,rss,etimes,args |
            grep '[p]ipeline_agent' || true
    } >"$result_dir/processes.txt"
    set +e
    if sudo -n true >/dev/null 2>&1; then
        sudo -n timeout "$hardware_seconds" /opt/eswin/bin/es_hw_watcher \
            >"$result_dir/es_hw_watcher.log" 2>&1
    else
        timeout "$hardware_seconds" /opt/eswin/bin/es_hw_watcher \
            >"$result_dir/es_hw_watcher.log" 2>&1
    fi
    local rc=$?
    set -e
    [[ $rc -eq 0 || $rc -eq 124 ]] ||
        die "es_hw_watcher 执行失败，rc=$rc"
}

critical_error_count() {
    grep -aEic \
        'Cannot allocate memory|create pool failed|start failed|worker exited|DetectionOut|FATAL|package decrypt failed|Segmentation fault' \
        "$agent_log" 2>/dev/null || true
}

restore_real_platform() {
    local should_restore=0
    case "$restore_real" in
        always) should_restore=1 ;;
        auto) [[ $previous_real -eq 1 ]] && should_restore=1 ;;
        never) ;;
    esac
    [[ $should_restore -eq 1 ]] || return 0
    log "恢复真实平台模式"
    "$real_starter" \
        --restart \
        --socket "$real_socket" \
        --wait-seconds 20
}

cleanup_test() {
    [[ $cleanup_done -eq 0 ]] || return 0
    set +e
    if [[ -S $control_socket ]]; then
        mock_command stop-all >/dev/null 2>&1
    fi
    if [[ -n $agent_pid ]] && kill -0 "$agent_pid" 2>/dev/null; then
        kill -TERM "$agent_pid" 2>/dev/null
        wait_process_exit "$agent_pid" "$stop_timeout"
        kill -KILL "$agent_pid" 2>/dev/null
    fi
    local worker_pids
    worker_pids=$(pgrep -f "$agent_binary --worker" 2>/dev/null)
    if [[ -n $worker_pids ]]; then
        kill -TERM $worker_pids 2>/dev/null
        sleep 2
        worker_pids=$(pgrep -f "$agent_binary --worker" 2>/dev/null)
        [[ -z $worker_pids ]] || kill -KILL $worker_pids 2>/dev/null
    fi
    if [[ -S $control_socket ]]; then
        mock_command shutdown >/dev/null 2>&1
    fi
    if [[ -n $server_pid ]] && kill -0 "$server_pid" 2>/dev/null; then
        wait_process_exit "$server_pid" 5
        kill -TERM "$server_pid" 2>/dev/null
    fi
    rm -f -- "$platform_socket" "$control_socket"
    cleanup_done=1
    set -e
}

on_exit() {
    local rc=$?
    trap - EXIT INT TERM
    if [[ $cleanup_done -eq 0 ]]; then
        cleanup_test || true
        restore_real_platform || true
    fi
    if [[ -n $summary_file && -d ${result_dir:-/nonexistent} &&
          ! -f $summary_file ]]; then
        {
            echo "state=failed"
            echo "exit_code=$rc"
            echo "result_dir=$result_dir"
        } >"$summary_file"
    fi
    exit "$rc"
}

trap on_exit EXIT
trap 'exit 130' INT TERM

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --channels) channels=$2; shift 2 ;;
        --start-index) start_index=$2; shift 2 ;;
        --stream-prefix) stream_prefix=$2; shift 2 ;;
        --rtsp-base) rtsp_base=$2; shift 2 ;;
        --rtsp-name-prefix) rtsp_name_prefix=$2; shift 2 ;;
        --package) package_path=$2; shift 2 ;;
        --scenario) scenario=$2; shift 2 ;;
        --threshold) threshold=$2; shift 2 ;;
        --alarm-level) alarm_level=$2; shift 2 ;;
        --record-duration) record_duration=$2; shift 2 ;;
        --dedup-interval) dedup_interval=$2; shift 2 ;;
        --task-interval) task_interval=$2; shift 2 ;;
        --settle-seconds) settle_seconds=$2; shift 2 ;;
        --operation-wait) operation_wait=$2; shift 2 ;;
        --startup-timeout) startup_timeout=$2; shift 2 ;;
        --worker-timeout) worker_timeout=$2; shift 2 ;;
        --stop-timeout) stop_timeout=$2; shift 2 ;;
        --churn-index) churn_index=$2; shift 2 ;;
        --skip-churn) churn_index=0; shift ;;
        --multi-event-index) multi_event_index=$2; shift 2 ;;
        --extra-scenario) extra_scenario=$2; shift 2 ;;
        --extra-package) extra_package=$2; shift 2 ;;
        --skip-multi-event) multi_event_index=0; shift ;;
        --evidence-mode) evidence_mode=$2; shift 2 ;;
        --evidence-timeout) evidence_timeout=$2; shift 2 ;;
        --expected-size)
            [[ $2 =~ ^([0-9]+)x([0-9]+)$ ]] ||
                die "--expected-size 格式必须为 WxH"
            expected_width=${BASH_REMATCH[1]}
            expected_height=${BASH_REMATCH[2]}
            shift 2
            ;;
        --hardware-seconds) hardware_seconds=$2; shift 2 ;;
        --skip-hardware) check_hardware=0; shift ;;
        --mmz-mode) mmz_mode=$2; shift 2 ;;
        --result-dir) result_dir=$2; shift 2 ;;
        --restore-real) restore_real=$2; shift 2 ;;
        --real-socket) real_socket=$2; shift 2 ;;
        --dry-run) dry_run=1; shift ;;
        *) die "未知参数或缺少参数值：$1" ;;
    esac
done

for value in "$channels" "$start_index" "$alarm_level" "$record_duration" \
    "$dedup_interval" "$task_interval" "$settle_seconds" "$operation_wait" \
    "$startup_timeout" "$worker_timeout" "$stop_timeout" "$evidence_timeout" \
    "$hardware_seconds" "$churn_index" "$multi_event_index"; do
    is_uint "$value" || die "整数参数无效：$value"
done
is_number "$threshold" || die "阈值无效：$threshold"
awk -v value="$threshold" 'BEGIN {exit !(value >= 0 && value <= 1)}' ||
    die "阈值必须在 [0,1]"
(( channels >= 1 && channels <= 64 )) || die "channels 必须在 1-64"
(( alarm_level >= 1 && alarm_level <= 4 )) || die "alarm-level 必须在 1-4"
(( start_index >= 1 )) || die "start-index 必须大于 0"
(( churn_index == 0 || (churn_index >= 1 && churn_index <= channels) )) ||
    die "churn-index 必须为 0 或 1..channels"
(( multi_event_index == 0 ||
   (multi_event_index >= 1 && multi_event_index <= channels) )) ||
    die "multi-event-index 必须为 0 或 1..channels"
[[ $extra_scenario != "$scenario" || $multi_event_index -eq 0 ]] ||
    die "第二事件编码不能与主事件相同"
[[ $evidence_mode =~ ^(all|any|off)$ ]] ||
    die "evidence-mode 必须为 all、any 或 off"
[[ $mmz_mode =~ ^(strict|observe|off)$ ]] ||
    die "mmz-mode 必须为 strict、observe 或 off"
[[ $restore_real =~ ^(auto|always|never)$ ]] ||
    die "restore-real 必须为 auto、always 或 never"
[[ -n $extra_package ]] || extra_package=$package_path

run_id="pipeline_mock_validation_$(date +%Y%m%d_%H%M%S)_$$"
[[ -n $result_dir ]] || result_dir="/tmp/$run_id"
if [[ -e $result_dir ]] &&
   [[ -n $(find "$result_dir" -mindepth 1 -maxdepth 1 -print -quit 2>/dev/null) ]]; then
    die "结果目录已存在且非空：$result_dir"
fi
platform_socket="$result_dir/platform.sock"
control_socket="$result_dir/control.sock"
runtime_dir="$result_dir/runtime"
evidence_dir="$result_dir/evidence"
server_log="$result_dir/mock-server.log"
agent_log="$result_dir/pipeline-agent.log"
summary_file="$result_dir/summary.env"

[[ -x $agent_binary ]] || die "缺少 pipeline_agent：$agent_binary"
[[ -x $launcher ]] || die "启动器不可执行：$launcher"
[[ -x $real_starter ]] || die "真实平台启动脚本不可执行：$real_starter"
[[ -r $mock ]] || die "缺少模拟器：$mock"
[[ -r $package_path ]] || die "主 pkg 不可读：$package_path"
[[ -r $extra_package ]] || die "第二事件 pkg 不可读：$extra_package"
command -v python3 >/dev/null || die "缺少 python3"
if [[ $evidence_mode != off ]]; then
    command -v file >/dev/null || die "缺少 file"
    command -v ffprobe >/dev/null || die "缺少 ffprobe"
fi
if [[ $check_hardware -eq 1 ]]; then
    [[ -x /opt/eswin/bin/es_hw_watcher ]] ||
        die "缺少 es_hw_watcher"
fi

cat <<EOF
Pipeline 模拟平台全流程验证
  channels          : $channels
  streams           : $(stream_id_for_position 1) .. $(stream_id_for_position "$channels")
  rtsp              : $(rtsp_url_for_position 1) ..
  scenario          : $scenario
  package           : $package_path
  churn_index       : $churn_index
  multi_event_index : $multi_event_index
  extra_scenario    : $extra_scenario
  evidence_mode     : $evidence_mode
  mmz_mode          : $mmz_mode
  restore_real      : $restore_real
  result_dir        : $result_dir
EOF

if [[ $dry_run -eq 1 ]]; then
    echo "dry-run 检查通过"
    cleanup_done=1
    trap - EXIT INT TERM
    exit 0
fi

mkdir -p -- "$result_dir" "$evidence_dir"
detect_previous_real
log "测试前真实平台管理进程：$previous_real"
stop_all_pipeline_processes
capture_vb baseline
baseline_free=
[[ $mmz_mode == off ]] ||
    baseline_free=$(vb_free <"$result_dir/vb.baseline.txt")

log "启动模拟平台"
python3 "$mock" serve \
    --platform-socket "$platform_socket" \
    --control-socket "$control_socket" \
    >"$server_log" 2>&1 &
server_pid=$!
for ((elapsed = 0; elapsed < startup_timeout; ++elapsed)); do
    [[ -S $platform_socket && -S $control_socket ]] && break
    kill -0 "$server_pid" 2>/dev/null || die "模拟平台提前退出"
    sleep 1
done
[[ -S $platform_socket && -S $control_socket ]] ||
    die "模拟平台 socket 创建超时"

log "启动 Pipeline 管理进程"
PIPELINE_PLATFORM_SOCKET="$platform_socket" \
PIPELINE_AGENT_RUNTIME_DIR="$runtime_dir" \
PIPELINE_ALARM_RELAY_SOCKET="$runtime_dir/alarm.sock" \
PIPELINE_AGENT_ID=agent_001 \
"$launcher" >"$agent_log" 2>&1 &
agent_pid=$!

connected=0
for ((elapsed = 0; elapsed < startup_timeout; ++elapsed)); do
    kill -0 "$agent_pid" 2>/dev/null || die "Pipeline 管理进程提前退出"
    if mock_command status >"$result_dir/status.initial.json" 2>/dev/null &&
       grep -q '"pipeline_connected": true' "$result_dir/status.initial.json"; then
        connected=1
        break
    fi
    sleep 1
done
[[ $connected -eq 1 ]] || die "Pipeline 未连接模拟平台"
[[ $(manager_count) -eq 1 ]] || die "模拟管理进程数量异常"

log "逐路启动 $channels 个任务"
for ((position = 1; position <= channels; ++position)); do
    start_stream "$position" >>"$result_dir/commands.log"
    sleep "$task_interval"
done
wait_for_worker_count "$channels"
sleep "$settle_seconds"
worker_map >"$result_dir/workers.started.txt"
capture_vb running

if (( churn_index > 0 )); then
    log "验证第 $churn_index 路单独停止和恢复"
    cp "$result_dir/workers.started.txt" "$result_dir/workers.churn.before.txt"
    stop_stream "$churn_index" >>"$result_dir/commands.log"
    wait_for_worker_count "$((channels - 1))"
    sleep "$operation_wait"
    worker_map >"$result_dir/workers.churn.stopped.txt"
    assert_base_workers_unchanged \
        "$result_dir/workers.churn.before.txt" \
        "$result_dir/workers.churn.stopped.txt" \
        "$churn_index"
    stopped_stream=$(stream_id_for_position "$churn_index")
    [[ -z $(pid_from_map "$result_dir/workers.churn.stopped.txt" \
        "$stopped_stream" "$scenario") ]] ||
        die "停止任务后 worker 仍存在：$stopped_stream"

    old_pid=$(pid_from_map "$result_dir/workers.churn.before.txt" \
        "$stopped_stream" "$scenario")
    start_stream "$churn_index" >>"$result_dir/commands.log"
    wait_for_worker_count "$channels"
    sleep "$operation_wait"
    worker_map >"$result_dir/workers.churn.restored.txt"
    assert_base_workers_unchanged \
        "$result_dir/workers.churn.before.txt" \
        "$result_dir/workers.churn.restored.txt" \
        "$churn_index"
    new_pid=$(pid_from_map "$result_dir/workers.churn.restored.txt" \
        "$stopped_stream" "$scenario")
    [[ -n $new_pid && $new_pid != "$old_pid" ]] ||
        die "恢复任务未创建新 worker：$stopped_stream"
fi

if (( multi_event_index > 0 )); then
    log "验证第 $multi_event_index 路多事件增删"
    worker_map >"$result_dir/workers.multievent.before.txt"
    start_stream_multi_event "$multi_event_index" >>"$result_dir/commands.log"
    wait_for_worker_count "$((channels + 1))"
    sleep "$operation_wait"
    worker_map >"$result_dir/workers.multievent.added.txt"
    assert_base_workers_unchanged \
        "$result_dir/workers.multievent.before.txt" \
        "$result_dir/workers.multievent.added.txt"
    multi_stream=$(stream_id_for_position "$multi_event_index")
    [[ -n $(pid_from_map "$result_dir/workers.multievent.added.txt" \
        "$multi_stream" "$extra_scenario") ]] ||
        die "第二事件 worker 未创建"

    start_stream "$multi_event_index" >>"$result_dir/commands.log"
    wait_for_worker_count "$channels"
    sleep "$operation_wait"
    worker_map >"$result_dir/workers.multievent.removed.txt"
    assert_base_workers_unchanged \
        "$result_dir/workers.multievent.before.txt" \
        "$result_dir/workers.multievent.removed.txt"
    [[ -z $(pid_from_map "$result_dir/workers.multievent.removed.txt" \
        "$multi_stream" "$extra_scenario") ]] ||
        die "第二事件 worker 未移除"
fi

log "等待并验证证据"
wait_for_evidence
validate_evidence
capture_hardware
mock_command status >"$result_dir/status.running.json"

heartbeat_count=$(grep -c '"type": 1' "$server_log" 2>/dev/null || true)
ack_count=$(grep -c '"type": 3' "$server_log" 2>/dev/null || true)
alarm_count=$(grep -c '"type": 5' "$server_log" 2>/dev/null || true)
(( heartbeat_count >= 1 )) || die "未收到心跳"
(( ack_count >= config_commands )) ||
    die "ACK 数量不足：期望至少 $config_commands，实际 $ack_count"
if [[ $evidence_mode != off ]]; then
    (( alarm_count >= 1 )) || die "生成证据后未收到告警"
fi
errors=$(critical_error_count)
(( errors == 0 )) || die "发现 $errors 条关键错误"

log "停止全部模拟任务"
active_before_stop=$channels
mock_command stop-all >"$result_dir/stop-all.json"
config_commands=$((config_commands + active_before_stop))
wait_for_worker_count 0
sleep "$operation_wait"
[[ $(manager_count) -eq 1 ]] || die "stop-all 后管理进程退出"
capture_vb stopped
final_free=
[[ $mmz_mode == off ]] ||
    final_free=$(vb_free <"$result_dir/vb.stopped.txt")
if [[ $mmz_mode == strict && $final_free != "$baseline_free" ]]; then
    die "MMZ 未恢复基线：baseline=$baseline_free final=$final_free"
fi

mock_command status >"$result_dir/status.stopped.json"
ack_count=$(grep -c '"type": 3' "$server_log" 2>/dev/null || true)
for ((elapsed = 0; elapsed < 10 && ack_count < config_commands; ++elapsed)); do
    sleep 1
    ack_count=$(grep -c '"type": 3' "$server_log" 2>/dev/null || true)
done
(( ack_count >= config_commands )) ||
    die "最终 ACK 数量不足：期望至少 $config_commands，实际 $ack_count"
errors=$(critical_error_count)
(( errors == 0 )) || die "停止阶段发现 $errors 条关键错误"

{
    echo "state=completed"
    echo "channels=$channels"
    echo "config_commands=$config_commands"
    echo "heartbeat_count=$heartbeat_count"
    echo "ack_count=$ack_count"
    echo "alarm_count=$alarm_count"
    echo "critical_error_count=$errors"
    echo "baseline_mmz_free=$baseline_free"
    echo "final_mmz_free=$final_free"
    echo "result_dir=$result_dir"
} >"$summary_file"

cleanup_test
restore_real_platform
cleanup_done=1
trap - EXIT INT TERM

log "模拟平台全流程验证通过"
cat "$summary_file"
