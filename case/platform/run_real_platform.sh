#!/usr/bin/env bash

set -Eeuo pipefail

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
pipeline_root=$(cd -- "$script_dir/../.." && pwd)
launcher="$script_dir/launch_pipeline_agent.sh"

platform_socket=${PIPELINE_PLATFORM_SOCKET:-/opt/smart-guard/run/media-agent/media_agent.sock}
runtime_dir=${PIPELINE_AGENT_RUNTIME_DIR:-/tmp/pipeline-agent}
relay_socket=${PIPELINE_ALARM_RELAY_SOCKET:-}
agent_id=${PIPELINE_AGENT_ID:-agent_001}
log_file=${PIPELINE_PLATFORM_LOG:-/tmp/pipeline-platform.log}
pid_file=${PIPELINE_PLATFORM_PID_FILE:-/tmp/pipeline-platform.pid}
service_name=${PIPELINE_PLATFORM_SERVICE:-smart-guard-edge.service}
evidence_root=${PIPELINE_EVIDENCE_ROOT:-/mnt/userdata/smart-guard-edge/recordings/default/ai-events}
wait_seconds=15
stop_timeout=30
restart=0
clean_runtime=1
foreground=0
skip_service_check=0
fix_evidence_permissions=0
dry_run=0

usage() {
    cat <<'EOF'
用法：
  run_real_platform.sh [选项]

运行方式：
      --restart                 停止现有 pipeline_agent 后重新启动
      --foreground              前台运行，适合直接观察日志
      --wait-seconds N          后台启动连接等待时间，默认 15
      --stop-timeout N          重启时优雅停止等待时间，默认 30
      --dry-run                 只打印检查结果和启动配置

路径与平台：
      --socket PATH             真实平台 Unix socket
      --runtime-dir DIR         Pipeline 运行目录
      --relay-socket PATH       worker 告警中继 socket
      --log FILE                后台日志文件
      --pid-file FILE           管理进程 PID 文件
      --agent-id ID             平台 Agent ID
      --service NAME            平台 systemd 服务名
      --skip-service-check      不检查 systemd 服务状态

运行目录和证据：
      --clean-runtime           启动前清理运行目录，默认启用
      --no-clean-runtime        保留现有运行目录
      --fix-evidence-permissions
                                为当前用户补充证据目录 ACL
      --evidence-root DIR       证据根目录

资源和日志参数可继续通过环境变量控制：
  PIPELINE_HEARTBEAT_MS
  PIPELINE_CONFIG_DEBOUNCE_MS
  PIPELINE_CONFIG_MAX_WAIT_MS
  PIPELINE_WORKER_STOP_TIMEOUT_MS
  PIPELINE_VDEC_POOL_SIZE
  PIPELINE_MUX_POOL_SIZE
  PIPELINE_PREPROCESS_POOL_SIZE
  PIPELINE_INFER_OUTPUT_POOL_SIZE
  PERF_STATIC_FLAG
  PL_LOG_LEVEL
  PIPELINE_WORKER_PERF_STATIC_FLAG
  PIPELINE_WORKER_LOG_LEVEL
EOF
}

die() {
    echo "错误：$*" >&2
    exit 1
}

is_uint() {
    [[ $1 =~ ^[0-9]+$ ]]
}

real_manager_pids() {
    local pid cmdline
    for pid in $(pgrep -x pipeline_agent 2>/dev/null || true); do
        [[ -r /proc/$pid/cmdline ]] || continue
        cmdline=$(tr '\0' ' ' <"/proc/$pid/cmdline")
        if [[ $cmdline == *"--platform-socket $platform_socket"* ]] &&
           [[ $cmdline != *" --worker "* ]]; then
            echo "$pid"
        fi
    done
}

all_pipeline_pids() {
    pgrep -x pipeline_agent 2>/dev/null || true
}

stop_existing() {
    local pids elapsed
    pids=$(all_pipeline_pids)
    [[ -n $pids ]] || return 0
    echo "停止现有 pipeline_agent：$(echo "$pids" | tr '\n' ' ')"
    kill -TERM $pids 2>/dev/null || true
    elapsed=0
    while [[ -n $(all_pipeline_pids) && $elapsed -lt $stop_timeout ]]; do
        sleep 1
        elapsed=$((elapsed + 1))
    done
    pids=$(all_pipeline_pids)
    if [[ -n $pids ]]; then
        echo "优雅停止超时，发送 SIGKILL：$(echo "$pids" | tr '\n' ' ')" >&2
        kill -KILL $pids 2>/dev/null || true
        sleep 1
    fi
    [[ -z $(all_pipeline_pids) ]] || die "仍有 pipeline_agent 未退出"
}

clean_runtime_dir() {
    [[ $clean_runtime -eq 1 ]] || return 0
    case "$runtime_dir" in
        /tmp/*) ;;
        *) die "为避免误删，只自动清理 /tmp 下的运行目录：$runtime_dir" ;;
    esac
    rm -rf -- "$runtime_dir"
}

fix_evidence_acl() {
    [[ $fix_evidence_permissions -eq 1 ]] || return 0
    [[ -d $evidence_root ]] || die "证据目录不存在：$evidence_root"
    command -v setfacl >/dev/null 2>&1 || die "未安装 setfacl"
    sudo -n true >/dev/null 2>&1 || die "修复证据权限需要免交互 sudo"
    sudo -n setfacl -R -m "u:$(id -un):rwx" "$evidence_root"
    sudo -n find "$evidence_root" -type d \
        -exec setfacl -m "d:u:$(id -un):rwx" {} +
}

print_config() {
    cat <<EOF
Pipeline 真实平台启动配置
  pipeline_root : $pipeline_root
  platform_socket: $platform_socket
  runtime_dir   : $runtime_dir
  relay_socket  : $relay_socket
  agent_id      : $agent_id
  log_file      : $log_file
  pid_file      : $pid_file
  service       : $service_name
  foreground    : $foreground
  restart       : $restart
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -h|--help)
            usage
            exit 0
            ;;
        --restart)
            restart=1
            shift
            ;;
        --foreground)
            foreground=1
            shift
            ;;
        --wait-seconds)
            [[ $# -ge 2 ]] || die "--wait-seconds 缺少参数"
            wait_seconds=$2
            shift 2
            ;;
        --stop-timeout)
            [[ $# -ge 2 ]] || die "--stop-timeout 缺少参数"
            stop_timeout=$2
            shift 2
            ;;
        --socket)
            [[ $# -ge 2 ]] || die "--socket 缺少参数"
            platform_socket=$2
            shift 2
            ;;
        --runtime-dir)
            [[ $# -ge 2 ]] || die "--runtime-dir 缺少参数"
            runtime_dir=$2
            shift 2
            ;;
        --relay-socket)
            [[ $# -ge 2 ]] || die "--relay-socket 缺少参数"
            relay_socket=$2
            shift 2
            ;;
        --log)
            [[ $# -ge 2 ]] || die "--log 缺少参数"
            log_file=$2
            shift 2
            ;;
        --pid-file)
            [[ $# -ge 2 ]] || die "--pid-file 缺少参数"
            pid_file=$2
            shift 2
            ;;
        --agent-id)
            [[ $# -ge 2 ]] || die "--agent-id 缺少参数"
            agent_id=$2
            shift 2
            ;;
        --service)
            [[ $# -ge 2 ]] || die "--service 缺少参数"
            service_name=$2
            shift 2
            ;;
        --skip-service-check)
            skip_service_check=1
            shift
            ;;
        --clean-runtime)
            clean_runtime=1
            shift
            ;;
        --no-clean-runtime)
            clean_runtime=0
            shift
            ;;
        --fix-evidence-permissions)
            fix_evidence_permissions=1
            shift
            ;;
        --evidence-root)
            [[ $# -ge 2 ]] || die "--evidence-root 缺少参数"
            evidence_root=$2
            shift 2
            ;;
        --dry-run)
            dry_run=1
            shift
            ;;
        *)
            die "未知参数：$1"
            ;;
    esac
done

is_uint "$wait_seconds" && [[ $wait_seconds -ge 1 ]] ||
    die "--wait-seconds 必须是正整数"
is_uint "$stop_timeout" && [[ $stop_timeout -ge 1 ]] ||
    die "--stop-timeout 必须是正整数"
[[ -n $relay_socket ]] || relay_socket="$runtime_dir/alarm.sock"

[[ -x $pipeline_root/bin/pipeline_agent ]] ||
    die "缺少可执行文件：$pipeline_root/bin/pipeline_agent"
[[ -x $launcher ]] || die "启动器不可执行：$launcher"
[[ -r $pipeline_root/config/Event.yaml ]] ||
    die "缺少配置：$pipeline_root/config/Event.yaml"
[[ -r $pipeline_root/config/EsTracker.yaml ]] ||
    die "缺少配置：$pipeline_root/config/EsTracker.yaml"
[[ -d $pipeline_root/lib ]] || die "缺少动态库目录：$pipeline_root/lib"

if [[ $skip_service_check -eq 0 ]]; then
    systemctl is-active --quiet "$service_name" ||
        die "平台服务未运行：$service_name"
fi
[[ -S $platform_socket ]] || die "平台 socket 不存在：$platform_socket"
[[ -w $platform_socket ]] || die "平台 socket 不可写：$platform_socket"

print_config
if [[ $dry_run -eq 1 ]]; then
    echo "dry-run 检查通过"
    exit 0
fi

existing_real=$(real_manager_pids)
existing_all=$(all_pipeline_pids)
if [[ -n $existing_all && $restart -eq 0 ]]; then
    if [[ -n $existing_real ]] &&
       [[ $(echo "$existing_all" | wc -w) -eq $(echo "$existing_real" | wc -w) ]]; then
        echo "真实平台模式已经运行，PID：$(echo "$existing_real" | tr '\n' ' ')"
        exit 0
    fi
    die "检测到其他 pipeline_agent；使用 --restart 明确重启"
fi
[[ $restart -eq 0 ]] || stop_existing

clean_runtime_dir
fix_evidence_acl
mkdir -p -- "$runtime_dir" "$(dirname -- "$log_file")" "$(dirname -- "$pid_file")"
rm -f -- "$pid_file"

export PIPELINE_PLATFORM_SOCKET="$platform_socket"
export PIPELINE_AGENT_RUNTIME_DIR="$runtime_dir"
export PIPELINE_ALARM_RELAY_SOCKET="$relay_socket"
export PIPELINE_AGENT_ID="$agent_id"

if [[ $foreground -eq 1 ]]; then
    exec "$launcher"
fi

: >"$log_file"
nohup setsid "$launcher" >"$log_file" 2>&1 </dev/null &
manager_pid=$!
echo "$manager_pid" >"$pid_file"

connected=0
for ((elapsed = 0; elapsed < wait_seconds; ++elapsed)); do
    if ! kill -0 "$manager_pid" 2>/dev/null; then
        tail -n 100 "$log_file" >&2 || true
        die "pipeline_agent 启动后提前退出"
    fi
    if grep -q '\[SocketSender\] connected to' "$log_file" 2>/dev/null &&
       [[ -S $relay_socket ]]; then
        connected=1
        break
    fi
    sleep 1
done

if [[ $connected -ne 1 ]]; then
    echo "连接平台超时，停止本次启动的管理进程" >&2
    kill -TERM "$manager_pid" 2>/dev/null || true
    sleep 2
    kill -KILL "$manager_pid" 2>/dev/null || true
    tail -n 100 "$log_file" >&2 || true
    exit 1
fi

actual_pid=$(real_manager_pids | head -n 1)
[[ -n $actual_pid ]] || die "未找到真实平台管理进程"
echo "$actual_pid" >"$pid_file"

echo "真实平台模式启动成功"
echo "  manager_pid : $actual_pid"
echo "  worker_count: $(pgrep -fc "$pipeline_root/bin/pipeline_agent --worker" || true)"
echo "  log         : $log_file"
echo "  runtime     : $runtime_dir"
tail -n 20 "$log_file"
