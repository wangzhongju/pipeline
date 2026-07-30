#!/bin/sh

set -eu

channels=${1:-26}
case "$channels" in
    ''|*[!0-9]*)
        echo "channels must be an integer from 1 to 26" >&2
        exit 2
        ;;
esac
if [ "$channels" -lt 1 ] || [ "$channels" -gt 26 ]; then
    echo "channels must be an integer from 1 to 26" >&2
    exit 2
fi

case_path=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
pipeline_root=$(CDPATH= cd -- "$case_path/../.." && pwd)
config_path="$case_path/config"
rtsp_config_path="$case_path/config_rtsp"

export PATH="$pipeline_root/bin:$PATH"
export LD_LIBRARY_PATH="$pipeline_root/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export PERF_STATIC_FLAG=${PERF_STATIC_FLAG:-1}
export PL_LOG_LEVEL=${PL_LOG_LEVEL:-1}

pipeline_semaphore=/dev/shm/sem.essdkpl_core_semaphore
if [ -e "$pipeline_semaphore" ] && ! pgrep -x espl_launch >/dev/null 2>&1; then
    rm -f "$pipeline_semaphore" 2>/dev/null || {
        echo "warning: cannot remove stale $pipeline_semaphore" >&2
    }
fi

if [ "${ENABLE_BUSINESS:-0}" = "1" ]; then
    platform_socket=/opt/smart-guard/run/media-agent/media_agent.sock
    if [ -S "$platform_socket" ] && [ ! -w "$platform_socket" ]; then
        echo "warning: $platform_socket is not writable; platform IPC will retry until permissions are fixed" >&2
    fi
fi

echo 1 > /proc/eswin/vb

ulimit -c unlimited
ulimit -n 65535
ulimit -l 1024000
ulimit -s 512000

mkdir -p "$rtsp_config_path"
i=1
while [ "$i" -le "$channels" ]; do
    cat >"$rtsp_config_path/EsAvDemux_rtsp_$i.yaml" <<EOF
url: rtsp://192.168.88.91/live/test$i
stream-id: test$i
outfps: 25
totalframe: 0
dump:
  enable: false
EOF
    i=$((i + 1))
done

cd "$pipeline_root"

set -- espl_launch \
    perfstat_interval "${PERFSTAT_INTERVAL_US:-60000000}" \
    config_path "$rtsp_config_path/" \
    lib_path "$pipeline_root/lib/"

i=1
while [ "$i" -le "$channels" ]; do
    set -- "$@" \
        EsAvDemux -path "EsAvDemux_rtsp_$i.yaml" -loopnum 200000000 - \
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
    ! EsPostProcess -name post1 -path "$config_path/EsPostProcess.yaml" -

if [ "${ENABLE_BUSINESS:-0}" = "1" ]; then
    set -- "$@" \
        ! EsTrackerLite -name track1 -path "$config_path/EsTrackerLite.yaml" - \
        ! EsEvent -name event1 -path "$config_path/EsEvent.yaml" -
fi

set -- "$@" \
    ! EsOsd -name osd1 -path "$config_path/EsOsd.yaml" - \
    ! EsQueue -name queuegrid1 -type 0 -deepth 5 - \
    ! EsVideoGrid -name videogrid1 -path "$config_path/EsVideoGridRtsp.yaml" - \
    ! EsTestSink -name sink1 -

exec "$@"
