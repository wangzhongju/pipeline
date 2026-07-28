#! /bin/sh

case_path=/opt/demo/pipeline/case/msi

export PATH=/opt/demo/pipeline/bin/:$PATH

export ES_SYSLOG=n

export LD_LIBRARY_PATH=/lib/:/usr/lib/:/usr/local/lib:/lib/acc-kit/:/usr/lib/riscv64-linux-gnu/:$LD_LIBRARY_PATH
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
cloopnum=200000000

time=$(date "+%Y-%m-%d %H:%M:%S")
echo "[PL_TIME_START: ]$time"

espl_launch perfstat_interval 10000000 config_path $case_path/config/  \
EsV4l2Src -name 'v4l2src1' -path EsV4l2Src.yaml - ! EsMux -name mux1 -timeout 40 -poolsize 16 - \
EsAvDemux -path EsAvDemux_19.yaml -loopnum $cloopnum - ! EsVdec -name decoder19 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_7.yaml  -loopnum $cloopnum  - ! EsVdec -name decoder7 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_21.yaml -loopnum $cloopnum  - ! EsVdec -name decoder21 -path EsVdec.yaml - ! element -name mux1 - \
! EsQueue -name queuepreproc1 -type 0 -deepth 5 - \
! EsPreProcess -name preproc1 -path EsPreProcess_channel1.yaml - \
! EsInfer -name infer1 -path EsInfer.yaml - \
! EsQueue -name queuepost1 -type 0 -deepth 5 - ! EsPostProcess -name post1 -path EsPostProcess.yaml - \
! EsQueue -name queuepreproc2 -type 0 -deepth 5 - \
! EsPreProcess -name preproc2 -path EsPreProcess_rtmpose.yaml - \
! EsInfer -name infer2 -path EsInfer_rtmpose.yaml - \
! EsQueue -name queuepost2 -type 0 -deepth 5 - ! EsPostProcess -name post2 -path EsPostProcess_rtmpose.yaml - \
! EsOsd -name osd1 -path EsOsd.yaml - \
! EsQueue -name queuegrid1 -type 0 -deepth 5 - \
! EsVideoGrid -name videogrid1 -path EsVideoGrid.yaml - \
! EsQueue -name queuevo1 -type 0 -deepth 5 - \
! EsOpenVo -name qt1 -path EsQt5.yaml - 

time=$(date "+%Y-%m-%d %H:%M:%S")
echo "[PL_TIME_START: ]$time"
