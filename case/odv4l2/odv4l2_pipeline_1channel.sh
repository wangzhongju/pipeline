#! /bin/sh

export PATH=/opt/demo/pipeline/bin/:$PATH
# export LD_LIBRARY_PATH=/lib/:/usr/lib/:/usr/local/lib:/lib/acc-kit/:/usr/lib/riscv64-linux-gnu/:$LD_LIBRARY_PATH
export PERF_STATIC_FLAG=0
export PL_LOG_LEVEL=4
# export essdk_log_config_path=/etc/es_syslog.conf
# export ES_SYSLOG=n

echo 1 > /proc/eswin/vb

ulimit -c unlimited
ulimit -n 65535
ulimit -l 1024000
ulimit -s 512000
#ulimit -a

time=$(date "+%Y-%m-%d %H:%M:%S")
echo "[PL_TIME_START: ]$time"

case_path=/opt/demo/pipeline/case/odv4l2
cloopnum=200000000

espl_launch perfstat_interval 10000000 config_path $case_path/config/ \
EsV4l2Src -name 'v4l2src1' -path EsV4l2Src_frame200.yaml - ! EsMux -name mux1 -timeout 30 - \
! EsQueue -name queuepreproc1 -type 0 -deepth 5 - \
! EsPreProcess -name preproc1 -path EsPreProcess_channel1.yaml - \
! EsInfer -name infer1 -path EsInfer.yaml - \
! EsQueue -name queuepost1 -type 0 -deepth 5 - \
! EsPostProcess -name post1 -path EsPostProcess.yaml - \
! EsTracker -name tracker1 -path EsTracker.yaml - \
! EsOsd -name osd1 -path EsOsd.yaml - \
! EsQueue -name queuegrid1 -type 0 -deepth 5 - \
! EsVideoGrid -name videogrid1 -path EsVideoGrid_1channel.yaml - \
! EsQueue -name queuevo1 -type 0 -deepth 5 - \
! EsOpenVo -name vo1 -path EsOpenVo.yaml -


time=$(date "+%Y-%m-%d %H:%M:%S")
echo "[PL_TIME_START: ]$time"
