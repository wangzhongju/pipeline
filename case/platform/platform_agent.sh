#!/bin/sh

set -eu

case_path=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
pipeline_root=$(CDPATH= cd -- "$case_path/../.." && pwd)
runtime_dir=${PIPELINE_AGENT_RUNTIME_DIR:-/tmp/pipeline-agent}
platform_socket=${PIPELINE_PLATFORM_SOCKET:-/opt/smart-guard/run/media-agent/media_agent.sock}
relay_socket=${PIPELINE_ALARM_RELAY_SOCKET:-$runtime_dir/alarm.sock}
agent_id=${PIPELINE_AGENT_ID:-agent_001}

if [ ! -S "$platform_socket" ]; then
    echo "platform socket not found: $platform_socket" >&2
    exit 1
fi
if [ ! -w "$platform_socket" ]; then
    echo "platform socket is not writable: $platform_socket" >&2
    exit 1
fi

export PATH="$pipeline_root/bin:$PATH"
export LD_LIBRARY_PATH="$pipeline_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PERF_STATIC_FLAG=${PERF_STATIC_FLAG:-1}
export PL_LOG_LEVEL=${PL_LOG_LEVEL:-4}

mkdir -p "$runtime_dir"
lock_file=$runtime_dir/pipeline_agent.lock
exec 9>"$lock_file"
if ! flock -n 9; then
    echo "pipeline_agent is already running (lock: $lock_file)" >&2
    exit 1
fi

ulimit -c unlimited
ulimit -n 65535
ulimit -l 1024000
ulimit -s 512000

exec pipeline_agent \
    --pipeline-root "$pipeline_root" \
    --runtime-dir "$runtime_dir" \
    --platform-socket "$platform_socket" \
    --relay-socket "$relay_socket" \
    --agent-id "$agent_id" \
    --heartbeat-ms "${PIPELINE_HEARTBEAT_MS:-10000}" \
    --config-debounce-ms "${PIPELINE_CONFIG_DEBOUNCE_MS:-2000}" \
    --config-max-wait-ms "${PIPELINE_CONFIG_MAX_WAIT_MS:-10000}" \
    --worker-stop-timeout-ms "${PIPELINE_WORKER_STOP_TIMEOUT_MS:-20000}" \
    --vdec-pool-size "${PIPELINE_VDEC_POOL_SIZE:-2}" \
    --mux-pool-size "${PIPELINE_MUX_POOL_SIZE:-8}" \
    --preprocess-pool-size "${PIPELINE_PREPROCESS_POOL_SIZE:-12}" \
    --infer-output-pool-size "${PIPELINE_INFER_OUTPUT_POOL_SIZE:-8}"
