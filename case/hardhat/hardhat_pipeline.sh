#!/bin/sh

set -eu

usage() {
    echo "usage: $0 STREAM_ID RTSP_URL [STREAM_ID RTSP_URL ...]" >&2
    echo "   or: STREAM_ID=id [RTSP_URL=url] $0" >&2
}

case_path=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
pipeline_root=$(CDPATH= cd -- "$case_path/../.." && pwd)
config_path="$case_path/config"
runtime_config_path="$case_path/config_runtime"

if [ "$#" -eq 0 ]; then
    if [ -z "${STREAM_ID:-}" ]; then
        usage
        exit 2
    fi
    set -- "$STREAM_ID" "${RTSP_URL:-rtsp://127.0.0.1:554/pull/$STREAM_ID}"
fi
if [ $(( $# % 2 )) -ne 0 ]; then
    usage
    exit 2
fi

model_path="$pipeline_root/models/260106_hardhat_cls2_512_b1_v1.model"
if [ ! -r "$model_path" ]; then
    echo "model not found: $model_path" >&2
    exit 1
fi

platform_socket=/opt/smart-guard/run/media-agent/media_agent.sock
if [ ! -S "$platform_socket" ]; then
    echo "platform socket not found: $platform_socket" >&2
    exit 1
fi
if [ ! -w "$platform_socket" ]; then
    echo "platform socket is not writable: $platform_socket" >&2
    echo "grant rw ACL to the pipeline user before running this script" >&2
    exit 1
fi

export PATH="$pipeline_root/bin:$PATH"
export LD_LIBRARY_PATH="$pipeline_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PERF_STATIC_FLAG=${PERF_STATIC_FLAG:-1}
export PL_LOG_LEVEL=${PL_LOG_LEVEL:-4}

pipeline_semaphore=/dev/shm/sem.essdkpl_core_semaphore
if [ -e "$pipeline_semaphore" ] && ! pgrep -x espl_launch >/dev/null 2>&1; then
    rm -f "$pipeline_semaphore" 2>/dev/null || {
        echo "cannot remove stale $pipeline_semaphore" >&2
        exit 1
    }
fi

if [ -w /proc/eswin/vb ]; then
    echo 1 > /proc/eswin/vb
else
    sudo sh -c 'echo 1 > /proc/eswin/vb'
fi

ulimit -c unlimited
ulimit -n 65535
ulimit -l 1024000
ulimit -s 512000

rm -rf "$runtime_config_path"
mkdir -p "$runtime_config_path"

channel=0
while [ "$#" -gt 0 ]; do
    channel=$((channel + 1))
    stream_id=$1
    rtsp_url=$2
    shift 2

    case "$stream_id" in
        ''|*[!A-Za-z0-9_.-]*)
            echo "invalid stream id: $stream_id" >&2
            exit 2
            ;;
    esac

    cat >"$runtime_config_path/EsAvDemux_$channel.yaml" <<EOF
url: $rtsp_url
stream-id: $stream_id
outfps: 25
totalframe: 0
dump:
  enable: false
EOF
done

cd "$pipeline_root"

set -- espl_launch \
    perfstat_interval "${PERFSTAT_INTERVAL_US:-10000000}" \
    config_path "$runtime_config_path/" \
    lib_path "$pipeline_root/lib/"

i=1
while [ "$i" -le "$channel" ]; do
    set -- "$@" \
        EsAvDemux -path "EsAvDemux_$i.yaml" -loopnum 200000000 - \
        ! EsVdec -name "decoder$i" -path "$config_path/EsVdec.yaml" -

    if [ "$i" -eq 1 ]; then
        set -- "$@" ! EsMux -name mux1 -timeout 40 -poolsize 16 -
    else
        set -- "$@" ! element -name mux1 -
    fi
    i=$((i + 1))
done

set -- "$@" \
    ! EsQueue -name queuepreproc1 -type 0 -deepth 5 - \
    ! EsPreProcess -name preproc1 -path "$config_path/EsPreProcess.yaml" - \
    ! EsInfer -name infer1 -path "$config_path/EsInfer.yaml" - \
    ! EsQueue -name queuepost1 -type 0 -deepth 5 - \
    ! EsPostProcess -name post1 -path "$config_path/EsPostProcess.yaml" - \
    ! EsTrackerLite -name track1 -path "$config_path/EsTrackerLite.yaml" - \
    ! EsEvent -name event1 -path "$config_path/EsEvent.yaml" - \
    ! EsTestSink -name sink1 -

exec "$@"
