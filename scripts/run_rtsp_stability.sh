#!/bin/sh

set -eu

channels=${1:-26}
duration=${2:-3600}
sample_interval=${3:-60}

case "$channels:$duration:$sample_interval" in
    *[!0-9:]*)
        echo "usage: $0 [channels:1-26] [duration_seconds] [sample_interval_seconds]" >&2
        exit 2
        ;;
esac
if [ "$channels" -lt 1 ] || [ "$channels" -gt 26 ] ||
   [ "$duration" -lt 1 ] || [ "$sample_interval" -lt 1 ]; then
    echo "invalid channels, duration, or sample interval" >&2
    exit 2
fi

pipeline_root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
case_script="$pipeline_root/case/od/od_pipeline_rtsp_stress.sh"
run_id="rtsp_${channels}ch_$(date +%Y%m%d_%H%M%S)"
result_root="$pipeline_root/stress_results/$run_id"
snapshot_root="$result_root/snapshots"

mkdir -p "$snapshot_root"
pipeline_log="$result_root/pipeline.log"
watcher_log="$result_root/es_hw_watcher.log"
status_file="$result_root/status.env"
summary_file="$result_root/summary.env"

pipeline_pid=
watcher_pid=
pipeline_exit=0
forced_stop=0

write_status() {
    state=$1
    elapsed=$2
    {
        echo "state=$state"
        echo "channels=$channels"
        echo "duration_seconds=$duration"
        echo "elapsed_seconds=$elapsed"
        echo "result_root=$result_root"
        echo "updated_at=$(date '+%F %T')"
    } >"$status_file.tmp"
    mv "$status_file.tmp" "$status_file"
}

stop_processes() {
    if [ -n "$watcher_pid" ] && kill -0 "$watcher_pid" 2>/dev/null; then
        kill "$watcher_pid" 2>/dev/null || true
        wait "$watcher_pid" 2>/dev/null || true
    fi
    if [ -n "$pipeline_pid" ] && kill -0 "$pipeline_pid" 2>/dev/null; then
        kill -INT "$pipeline_pid" 2>/dev/null || true
        wait_count=0
        while kill -0 "$pipeline_pid" 2>/dev/null && [ "$wait_count" -lt 60 ]; do
            sleep 1
            wait_count=$((wait_count + 1))
        done
        if kill -0 "$pipeline_pid" 2>/dev/null; then
            kill -KILL "$pipeline_pid" 2>/dev/null || true
        fi
        wait "$pipeline_pid" 2>/dev/null || true
    fi
}

stop_pipeline_and_wait() {
    if [ -z "$pipeline_pid" ]; then
        return
    fi

    if kill -0 "$pipeline_pid" 2>/dev/null; then
        kill -INT "$pipeline_pid" 2>/dev/null || true
        wait_count=0
        while kill -0 "$pipeline_pid" 2>/dev/null && [ "$wait_count" -lt 60 ]; do
            sleep 1
            wait_count=$((wait_count + 1))
        done
        if kill -0 "$pipeline_pid" 2>/dev/null; then
            forced_stop=1
            kill -KILL "$pipeline_pid" 2>/dev/null || true
        fi
    fi

    wait "$pipeline_pid" || pipeline_exit=$?
    pipeline_pid=
}
trap stop_processes EXIT INT TERM

write_status starting 0
"$case_script" "$channels" >"$pipeline_log" 2>&1 &
pipeline_pid=$!

startup_wait=0
while ! grep -q "will start WaitForFinish" "$pipeline_log" 2>/dev/null; do
    if ! kill -0 "$pipeline_pid" 2>/dev/null; then
        wait "$pipeline_pid" || pipeline_exit=$?
        pipeline_exit=${pipeline_exit:-0}
        write_status startup_failed "$startup_wait"
        {
            echo "state=startup_failed"
            echo "pipeline_exit=$pipeline_exit"
            echo "startup_wait_seconds=$startup_wait"
        } >"$summary_file"
        exit 1
    fi
    if [ "$startup_wait" -ge 180 ]; then
        write_status startup_timeout "$startup_wait"
        exit 1
    fi
    sleep 2
    startup_wait=$((startup_wait + 2))
    write_status starting "$startup_wait"
done

cycles=$((duration / sample_interval + 2))
sudo -n /opt/eswin/bin/es_hw_watcher -i "$sample_interval" -c "$cycles" >"$watcher_log" 2>&1 &
watcher_pid=$!

start_epoch=$(date +%s)
start_time=$(date '+%F %T')
write_status running 0

while :; do
    now_epoch=$(date +%s)
    elapsed=$((now_epoch - start_epoch))
    if [ "$elapsed" -ge "$duration" ]; then
        break
    fi
    if ! kill -0 "$pipeline_pid" 2>/dev/null; then
        write_status pipeline_exited "$elapsed"
        break
    fi

    stamp=$(date +%Y%m%d_%H%M%S)
    snapshot="$snapshot_root/$stamp.txt"
    {
        echo "timestamp=$(date '+%F %T')"
        echo "elapsed_seconds=$elapsed"
        echo "=== uptime ==="
        uptime
        echo "=== loadavg ==="
        cat /proc/loadavg
        echo "=== memory ==="
        free -b
        echo "=== pipeline_process ==="
        ps -o pid,ppid,stat,ni,psr,%cpu,%mem,rss,vsz,nlwp,etimes,cmd -p "$pipeline_pid"
        echo "=== temperatures ==="
        for zone in /sys/class/thermal/thermal_zone*; do
            [ -r "$zone/temp" ] || continue
            printf '%s type=' "$zone"
            cat "$zone/type" 2>/dev/null || true
            printf ' temp='
            cat "$zone/temp"
        done
        echo "=== network ==="
        cat /proc/net/dev
        echo "=== vb ==="
        cat /proc/eswin/vb
        for item in bms dec enc vo vps; do
            echo "=== esmap/$item ==="
            cat "/proc/esmap/$item" 2>&1 || true
        done
    } >"$snapshot"

    write_status running "$elapsed"
    sleep_for=$sample_interval
    remaining=$((duration - elapsed))
    if [ "$remaining" -lt "$sleep_for" ]; then
        sleep_for=$remaining
    fi
    sleep "$sleep_for"
done

end_epoch=$(date +%s)
active_seconds=$((end_epoch - start_epoch))
end_time=$(date '+%F %T')

stop_pipeline_and_wait

if [ -n "$watcher_pid" ] && kill -0 "$watcher_pid" 2>/dev/null; then
    kill "$watcher_pid" 2>/dev/null || true
fi
wait "$watcher_pid" 2>/dev/null || true
watcher_pid=

error_count=$(grep -Eci "pipeline init failed|dlopen\\(|ES_VPS_MultiSourcesBlit error|assert|segmentation|aborted|out of memory" "$pipeline_log" || true)
rtsp_open_failures=$(grep -Eci "open file or url .* failed|Could not open source" "$pipeline_log" || true)
snapshots=$(find "$snapshot_root" -type f | wc -l)

state=completed
if [ "$forced_stop" -eq 1 ]; then
    state=completed_forced_stop
fi
if [ "$active_seconds" -lt "$duration" ] ||
   { [ "$pipeline_exit" -ne 0 ] && [ "$forced_stop" -eq 0 ]; } ||
   [ "$error_count" -ne 0 ] || [ "$rtsp_open_failures" -ne 0 ]; then
    state=failed
fi

{
    echo "state=$state"
    echo "channels=$channels"
    echo "start_time=$start_time"
    echo "end_time=$end_time"
    echo "active_seconds=$active_seconds"
    echo "pipeline_exit=$pipeline_exit"
    echo "forced_stop=$forced_stop"
    echo "error_count=$error_count"
    echo "rtsp_open_failures=$rtsp_open_failures"
    echo "snapshots=$snapshots"
    echo "result_root=$result_root"
} >"$summary_file"
write_status "$state" "$active_seconds"

trap - EXIT INT TERM
[ "$state" = completed ] || [ "$state" = completed_forced_stop ]
