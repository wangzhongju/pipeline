#! /bin/sh

export PATH=/opt/demo/pipeline/bin/:$PATH
export PERF_STATIC_FLAG=0
export PL_LOG_LEVEL=4


ulimit -c unlimited
ulimit -n 65535
ulimit -l 1024000
ulimit -s 512000
#ulimit -a

case_path=/opt/demo/pipeline/case/dualDie/die1
cloopnum=200000000

numactl --membind=1 --cpunodebind=1 espl_launch perfstat_interval 1000000 config_path  $case_path/config/ \
EsAvDemux -path EsAvDemux1.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder1 -path EsVdec.yaml -die 1 - ! EsMux -name mux1 -timeout 40 -poolsize 16 -die 1 - \
EsAvDemux -path EsAvDemux2.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder2 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux3.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder3 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux4.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder4 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux5.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder5 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux6.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder6 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux7.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder7 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux8.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder8 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux9.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder9 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux10.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder10 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux11.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder11 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux12.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder12 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux13.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder13 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux14.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder14 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux15.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder15 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux16.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder16 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux17.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder17 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux18.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder18 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux19.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder19 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux20.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder20 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux21.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder21 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux22.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder22 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux23.yaml -loopnum $cloopnum -die 1- ! EsVdec -name decoder23 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux24.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder24 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
EsAvDemux -path EsAvDemux25.yaml -loopnum $cloopnum -die 1 - ! EsVdec -name decoder25 -path EsVdec.yaml -die 1 - ! element -name mux1 - \
! EsQueue -name queuepreproc1 -type 0 -deepth 5 -die 1 - \
! EsPreProcess -name preproc1 -path EsPreProcess.yaml -die 1 - \
! EsInfer -name infer1 -path EsInfer.yaml -die 1 - \
! EsQueue -name queuepost1 -type 0 -deepth 5 -die 1 - ! EsPostProcess -name post1 -path EsPostProcess.yaml -die 1 - \
! EsTracker -name tracker1 -path EsTracker.yaml -die 1 - \
! EsOsd -name osd1 -path EsOsd.yaml -die 1 - \
! EsQueue -name queuegrid1 -type 0 -deepth 5 -die 1 - \
! EsVideoGrid -name videogrid1 -path EsVideoGrid.yaml -die 1 - \
! EsQueue -name queuevo1 -type 0 -deepth 5 -die 1 - \
! EsVideoSink -name vo1 -path EsVideoSink.yaml -die 1 -
