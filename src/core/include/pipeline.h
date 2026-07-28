#ifndef _ESSDK_PL_PIPELINE_H__
#define _ESSDK_PL_PIPELINE_H__

#include <sched.h>
#include <stdio.h>
#include <sys/utsname.h>

#include <functional>
#include <thread>
#include <vector>

#include "batch_meta.h"
#include "defines.h"
#include "element.h"
#include "error.h"
#include "log.h"
#include "queue.h"

using namespace std;

extern volatile bool isAllElementStart;
extern volatile bool isVoEosFlag;

class CPipeLine {
   public:
    CPipeLine(string name = "unknown", bool flushFlag = false);
    ~CPipeLine() = default;

   public:
    // 把所有插件加入到pipeline中，这样可以统一对所有的element进行后续处理；
    app_ret AddToPipeline(CElement *ele1, ...);

    // 链接插件，链接后，所有插件可以知道和自己link的上下游插件；
    app_ret LinkMany(CElement *ele1, ...);

    // 调用element->init()接口，分配相应资源；
    virtual app_ret Init();

    // 调用element->start()接口，启动线程；
    app_ret Start();

    // 调用element->wait(), 等待线程结束；
    app_ret WaitForFinish();

    void PerfStatisticsTimer();
    app_ret SetStatInterval(int statInterval);

    // 停止所有插件并结束
    app_ret Finish();

    void setDieAffinety(int die);

    app_ret notifyExit();

   private:
    // zlog_category_t *mCat;
    vector<CElement *> mElements;  // 加入到pipeline中的所有element；

    std::thread kpiTid;
    bool perfStatRun;
    int mStatInterval;  // ms
};

class CPadElement : public CElement {
   public:
    CPadElement(PerformanceStatic *perf, bool isSrc) : mPerf(perf), mIsSrc(isSrc) {};
    ~CPadElement() {};
    app_ret ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) {
        unsigned int cnt = 1;
        if (baseMeta->mMetaType == BATCH_META) {
            cnt = ((CBatchMeta *)baseMeta)->getFrameMetaSize();
        }
        if (mIsSrc) {
            mPerf->dataInCount(cnt);
        } else {
            mPerf->dataOutCount(cnt);
        }
        return APP_SUCCESS;
    };
    app_ret ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *self) {
        ProcessData(baseMeta, this);
        TransMitToNextToProcess(baseMeta);
        return APP_SUCCESS;
    };

   private:
    PerformanceStatic *mPerf;
    bool mIsSrc;
};

#endif  //_ESSDK_PL_PIPELINE_H__
