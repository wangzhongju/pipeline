#! /bin/sh

export PATH=/opt/demo/pipeline/bin/:$PATH
# export LD_LIBRARY_PATH=/lib/:/usr/lib/:/usr/local/lib:/lib/acc-kit/:/usr/lib/riscv64-linux-gnu/:$LD_LIBRARY_PATH
export PERF_STATIC_FLAG=0
export PL_LOG_LEVEL=4
# export essdk_log_config_path=/etc/es_syslog.conf
# export ES_SYSLOG=n

mkdir -p /home/eswin/demo/pipeline/

echo 1 > /proc/eswin/vb

ulimit -c unlimited
ulimit -n 65535
ulimit -l 1024000
ulimit -s 512000
#ulimit -a

time=$(date "+%Y-%m-%d %H:%M:%S")
echo "[PL_TIME_START: ]$time"

case_path=/opt/demo/pipeline/case/nvr
cloopnum=200000000

espl_launch perfstat_interval 10000000 config_path $case_path/config/ \
EsAvDemux -path EsAvDemux_2.yaml -loopnum $cloopnum - ! EsVdec -name decoder1 -path EsVdec.yaml - ! EsMux -name mux1 -timeout 40 -poolsize 16 - \
EsAvDemux -path EsAvDemux_22.yaml -loopnum $cloopnum  - ! EsVdec -name decoder2 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_25.yaml -loopnum $cloopnum  - ! EsVdec -name decoder3 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_2.yaml -loopnum $cloopnum  - ! EsVdec -name decoder4 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_22.yaml -loopnum $cloopnum  - ! EsVdec -name decoder5 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_25.yaml -loopnum $cloopnum  - ! EsVdec -name decoder6 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_2.yaml -loopnum $cloopnum  - ! EsVdec -name decoder7 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_22.yaml -loopnum $cloopnum  - ! EsVdec -name decoder8 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_25.yaml -loopnum $cloopnum  - ! EsVdec -name decoder9 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_2.yaml -loopnum $cloopnum  - ! EsVdec -name decoder10 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_22.yaml -loopnum $cloopnum  - ! EsVdec -name decoder11 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_25.yaml -loopnum $cloopnum  - ! EsVdec -name decoder12 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_2.yaml -loopnum $cloopnum  - ! EsVdec -name decoder13 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_22.yaml -loopnum $cloopnum  - ! EsVdec -name decoder14 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_25.yaml -loopnum $cloopnum  - ! EsVdec -name decoder15 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_2.yaml -loopnum $cloopnum  - ! EsVdec -name decoder16 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_22.yaml -loopnum $cloopnum  - ! EsVdec -name decoder17 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_25.yaml -loopnum $cloopnum  - ! EsVdec -name decoder18 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_2.yaml -loopnum $cloopnum  - ! EsVdec -name decoder19 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_22.yaml -loopnum $cloopnum  - ! EsVdec -name decoder20 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_25.yaml -loopnum $cloopnum  - ! EsVdec -name decoder21 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_2.yaml -loopnum $cloopnum  - ! EsVdec -name decoder22 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_22.yaml -loopnum $cloopnum  - ! EsVdec -name decoder23 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_25.yaml -loopnum $cloopnum  - ! EsVdec -name decoder24 -path EsVdec.yaml - ! element -name mux1 - \
EsAvDemux -path EsAvDemux_2.yaml -loopnum $cloopnum  - ! EsVdec -name decoder25 -path EsVdec.yaml - ! element -name mux1 - \
! EsQueue -name queuepreproc1 -type 0 -deepth 5 - \
! EsPreProcess -name preproc1 -path EsPreProcess.yaml - \
! EsInfer -name infer1 -path EsInfer.yaml - \
! EsQueue -name queuepost1 -type 0 -deepth 5 - ! EsPostProcess -name post1 -path EsPostProcess.yaml - \
! EsTracker -name tracker1 -path EsTracker.yaml - \
! EsQueue -name queueosd1 -type 0 -deepth 5 - \
! EsOsd -name osd1 -path EsOsd.yaml - \
! EsTee -name tee1 - \
! EsQueue -name queuegrid1 -type 0 -deepth 5 - \
! EsVideoGrid -name videogrid1 -path EsVideoGrid.yaml - \
! EsQueue -name queuevo1 -type 0 -deepth 5 - \
! EsOpenVo -name vo1 -path EsOpenVo.yaml - \
element -name tee1 - \
! EsDemux -name demux1 - \
! EsQueue -name queue12 -type 0 -deepth 5 - ! EsVenc -name venc_1 -path EsVenc.yaml - ! EsFileSink -name sink1 -path EsFileSink.yaml - \
element -name demux1 - \
! EsQueue -name queue22 -type 0 -deepth 5 - ! EsVenc -name venc_2 -path EsVenc.yaml - ! EsFileSink -name sink2 -path EsFileSink.yaml - \
element -name demux1 - \
! EsQueue -name queue32 -type 0 -deepth 5 - ! EsVenc -name venc_3 -path EsVenc.yaml - ! EsFileSink -name sink3 -path EsFileSink.yaml - \
element -name demux1 - \
! EsQueue -name queue42 -type 0 -deepth 5 - ! EsVenc -name venc_4 -path EsVenc.yaml - ! EsFileSink -name sink4 -path EsFileSink.yaml - \
element -name demux1 - \
! EsQueue -name queue52 -type 0 -deepth 5 - ! EsVenc -name venc_5 -path EsVenc.yaml - ! EsFileSink -name sink5 -path EsFileSink.yaml - \
element -name demux1 - \
! EsQueue -name queue62 -type 0 -deepth 5 - ! EsVenc -name venc_6 -path EsVenc.yaml - ! EsFileSink -name sink6 -path EsFileSink.yaml - \
element -name demux1 - \
! EsQueue -name queue72 -type 0 -deepth 5 - ! EsVenc -name venc_7 -path EsVenc.yaml - ! EsFileSink -name sink7 -path EsFileSink.yaml - \
element -name demux1 - \
! EsQueue -name queue82 -type 0 -deepth 5 - ! EsVenc -name venc_8 -path EsVenc.yaml - ! EsFileSink -name sink8 -path EsFileSink.yaml - \
element -name demux1 - \
! EsQueue -name queue92 -type 0 -deepth 5 - ! EsVenc -name venc_9 -path EsVenc.yaml - ! EsFileSink -name sink9 -path EsFileSink.yaml - \
element -name demux1 - \
! EsQueue -name queue112 -type 0 -deepth 5 - ! EsVenc -name venc_10 -path EsVenc.yaml - ! EsFileSink -name sink10 -path EsFileSink.yaml - \
element -name demux1 - \
! EsQueue -name queue122 -type 0 -deepth 5 - ! EsVenc -name venc_11 -path EsVenc.yaml - ! EsFileSink -name sink11 -path EsFileSink.yaml - \
element -name demux1 - \
! EsQueue -name queue132 -type 0 -deepth 5 - ! EsVenc -name venc_12 -path EsVenc.yaml - ! EsFileSink -name sink12 -path EsFileSink.yaml - \
element -name demux1 - \
! EsQueue -name queue142 -type 0 -deepth 5 - ! EsVenc -name venc_13 -path EsVenc.yaml - ! EsFileSink -name sink13 -path EsFileSink.yaml - \
element -name demux1 - \
! EsQueue -name queue152 -type 0 -deepth 5 - ! EsVenc -name venc_14 -path EsVenc.yaml - ! EsFileSink -name sink14 -path EsFileSink.yaml -