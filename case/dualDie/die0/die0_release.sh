#! /bin/sh

export PATH=/opt/demo/pipeline/bin/:$PATH
export PERF_STATIC_FLAG=0
export PL_LOG_LEVEL=4

ulimit -c unlimited
ulimit -n 65535
ulimit -l 1024000
ulimit -s 512000
#ulimit -a

case_path=/opt/demo/pipeline/test_case/dualDie/die0
cloopnum=200000000

espl_launch perfstat_interval 10000000 config_path $case_path/config/ \
EsAvDemux -path EsAvDemux_2.yaml -loopnum $cloopnum - ! EsVdec -name decoder1 -path EsVdec.yaml - ! EsMux -name mux1 -timeout 30 - \
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
! EsOsd -name osd1 -path EsOsd.yaml - \
! EsQueue -name queuegrid1 -type 0 -deepth 5 - \
! EsVideoGrid -name videogrid1 -path EsVideoGrid.yaml - \
! EsQueue -name queuevo1 -type 0 -deepth 5 - \
! EsVideoSink -name vo1 -path EsVideoSink.yaml -
