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

case_path=/opt/demo/pipeline/case/od
cloopnum=200000000


# test case01:preprocess+inference+poseprocess
espl_launch  config_path ./config/  \
EsTestSrc -name src0 -path EsTestsrc.yaml -loopnum 10 -fps 1000 - \
! EsPreProcess -name preproc1 -path EsPreProcess.yaml - \
! EsInfer -name infer1 -path EsInfer.yaml - \
! EsPostProcess -name post1 -path EsPostProcess.yaml - \
! EsTestSink -

# test case02: preprocess
# espl_launch  config_path ./config/  \
# EsTestSrc -name src0 -path EsTestsrc.yaml -loopnum 10 -fps 1000 - \
# ! EsPreProcess -name preproc1 -path EsPreProcess.yaml - \
# ! EsTestSink -


#  test case03:  inference 
# espl_launch  config_path ./config/   \
# EsTestSrc -name src0 -path EsTestsrc.yaml -loopnum 10 -fps 1000 - \
# ! EsInfer -name infer1 -path EsInfer.yaml - \
# ! EsTestSink -

#  test case04: poseprocess
# espl_launch  config_path ./config/   \
# EsTestSrc -name src0 -path EsTestsrc.yaml -loopnum 10 -fps 1000 - \
# ! EsPostProcess -name post1 -path EsPostProcess.yaml - \
# ! EsTestSink -

#  test case05: venc
espl_launch  config_path ./config/   \
EsTestSrc -name src0 -path EsTestsrc.yaml -loopnum 10 -fps 1000 - \
! EsVenc -name venc_11 -path EsVenc.yaml - \
! EsFileSink -name sink11 -path EsFileSink.yaml - 

