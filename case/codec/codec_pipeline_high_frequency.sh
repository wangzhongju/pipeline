#! /bin/sh

WORK_DIR="/home/eswin/demo/pipeline/"
export PATH="/opt/demo/pipeline/bin/:$PATH:/opt/demo/pipeline/tools/"
# export LD_LIBRARY_PATH=/lib/:/usr/lib/:/usr/local/lib:/lib/acc-kit/:/usr/lib/riscv64-linux-gnu/:$LD_LIBRARY_PATH
export PERF_STATIC_FLAG=0
export PL_LOG_LEVEL=4
# export essdk_log_config_path=/etc/es_syslog.conf
# export ES_SYSLOG=n

echo 1 > /proc/eswin/vb

if [ -d "$WORK_DIR" ]; then
    echo "Directory $WORK_DIR exists. Clearing contents..."
    rm -rf "${WORK_DIR}"*
else
    echo "Directory $WORK_DIR does not exist. Creating it..."
    mkdir -p "$WORK_DIR"
fi

ulimit -c unlimited
ulimit -n 65535
ulimit -l 1024000
ulimit -s 512000
# ulimit -a

# support: 36 dec + 16 enc
devmem 0x518281c8 32 1
reset_register() {
    devmem 0x518281c8 32 0
}
trap reset_register EXIT INT

time=$(date "+%Y-%m-%d %H:%M:%S")
echo "[PL_TIME_START: ]$time"

case_path=/opt/demo/pipeline/case/codec
cloopnum=200000000

espl_launch perfstat_interval 200000 config_path $case_path/config/  \
EsAvDemux -path EsAvDemux_1.yaml -loopnum $cloopnum - ! EsVdec -name decoder1 -path EsVdec.yaml - \
! EsMux -name mux1 -timeout 40 -poolsize 16 - \
EsAvDemux -path EsAvDemux_2.yaml -loopnum $cloopnum - ! EsVdec -name decoder2 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_3.yaml -loopnum $cloopnum - ! EsVdec -name decoder3 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_4.yaml -loopnum $cloopnum - ! EsVdec -name decoder4 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_5.yaml -loopnum $cloopnum - ! EsVdec -name decoder5 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_6.yaml -loopnum $cloopnum - ! EsVdec -name decoder6 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_7.yaml -loopnum $cloopnum - ! EsVdec -name decoder7 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_8.yaml -loopnum $cloopnum - ! EsVdec -name decoder8 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_9.yaml -loopnum $cloopnum - ! EsVdec -name decoder9 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_10.yaml -loopnum $cloopnum - ! EsVdec -name decoder10 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_11.yaml -loopnum $cloopnum - ! EsVdec -name decoder11 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_12.yaml -loopnum $cloopnum - ! EsVdec -name decoder12 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_13.yaml -loopnum $cloopnum - ! EsVdec -name decoder13 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_14.yaml -loopnum $cloopnum - ! EsVdec -name decoder14 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_15.yaml -loopnum $cloopnum - ! EsVdec -name decoder15 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_16.yaml -loopnum $cloopnum - ! EsVdec -name decoder16 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_17.yaml -loopnum $cloopnum - ! EsVdec -name decoder17 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_18.yaml -loopnum $cloopnum - ! EsVdec -name decoder18 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_19.yaml -loopnum $cloopnum - ! EsVdec -name decoder19 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_20.yaml -loopnum $cloopnum - ! EsVdec -name decoder20 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_21.yaml -loopnum $cloopnum - ! EsVdec -name decoder21 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_22.yaml -loopnum $cloopnum - ! EsVdec -name decoder22 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_23.yaml -loopnum $cloopnum - ! EsVdec -name decoder23 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_24.yaml -loopnum $cloopnum - ! EsVdec -name decoder24 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_25.yaml -loopnum $cloopnum - ! EsVdec -name decoder25 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_26.yaml -loopnum $cloopnum - ! EsVdec -name decoder26 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_27.yaml -loopnum $cloopnum - ! EsVdec -name decoder27 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_28.yaml -loopnum $cloopnum - ! EsVdec -name decoder28 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_29.yaml -loopnum $cloopnum - ! EsVdec -name decoder29 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_30.yaml -loopnum $cloopnum - ! EsVdec -name decoder30 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_31.yaml -loopnum $cloopnum - ! EsVdec -name decoder31 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_32.yaml -loopnum $cloopnum - ! EsVdec -name decoder32 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_33.yaml -loopnum $cloopnum - ! EsVdec -name decoder33 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_34.yaml -loopnum $cloopnum - ! EsVdec -name decoder34 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_35.yaml -loopnum $cloopnum - ! EsVdec -name decoder35 -path EsVdec.yaml - \
! element -name mux1 - \
EsAvDemux -path EsAvDemux_36.yaml -loopnum $cloopnum - ! EsVdec -name decoder36 -path EsVdec.yaml - \
! element -name mux1 - \
! EsQueue -name queueTee -type 0 -deepth 5 - \
! EsTee -name tee1 - \
! EsOsd -name osd1 -path EsOsd.yaml - \
! EsQueue -name queueTee11 -type 0 -deepth 5 - \
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
! EsQueue -name queue152 -type 0 -deepth 5 - ! EsVenc -name venc_14 -path EsVenc.yaml - ! EsFileSink -name sink14 -path EsFileSink.yaml - \
element -name demux1 - \
! EsQueue -name queue162 -type 0 -deepth 5 - ! EsVenc -name venc_15 -path EsVenc.yaml - ! EsFileSink -name sink15 -path EsFileSink.yaml - \
element -name demux1 - \
! EsQueue -name queue172 -type 0 -deepth 5 - ! EsVenc -name venc_16 -path EsVenc.yaml - ! EsFileSink -name sink16 -path EsFileSink.yaml - \
element -name tee1 - \
! EsQueue -name queueTee12 -type 0 -deepth 5 - \
! EsVideoGrid -name videogrid1 -path EsVideoGrid.yaml - \
! EsQueue -name queueTee21 -type 0 -deepth 5 - \
! EsVideoSink -name vo1 -path EsVideoSink.yaml -

time=$(date "+%Y-%m-%d %H:%M:%S")
echo "[PL_TIME_START: ]$time"