#ifndef _OSD_ELEMENT_H__
#define _OSD_ELEMENT_H__
#include <chrono>
#include <mutex>

#include "./common/esosd.h"
#include "batch_meta.h"
#include "dx/dx.h"
#include "element.h"
#include "yaml_parser.h"

class OsdElement : public CElement {
   public:
    OsdElement(string name = "element", string config = "", int dieIndex = 0)
        : CElement(name.c_str(), config.c_str(), dieIndex) {
        mPerfType = SYNC_PERF_ELEMENT;
        string dumpPath = mDumpBasePath + "/osd";
        mDbg = new dx::Debugger(dumpPath.c_str());
        mInputDumpFile = name + "_" + "input.dx";
        mOutputDumpFile = name + "_" + "output.dx";
    };
    ~OsdElement() {};

    app_ret Init() override;
    app_ret Start() override;
    app_ret ProcessData(CBaseMeta* baseMeta, CElement const* previousElement) override;
    // app_ret TransMitToNextToProcess(CBaseMeta *baseMeta);
    // app_ret ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *self =
    // 0);
    app_ret perfStat() override;
    app_ret Finish() override;

   private:
    app_ret ProcessBatchMeta(CBatchMeta* batchMeta);
    app_ret ProcessFrameMeta(CFrameMeta* frameMeta);
    app_ret ProcessGridMeta(CVideoGridMeta* gridMeta);

   private:
    std::mutex mtx;
    OSD_PARAM_S mOsdParam;
    OsdProc* mOsdProc;
    Color rectColor;
    Color textColor;

    Color RtmOsdColor;

    time_t baseTime;

    dx::Debugger* mDbg;
    string mInputDumpFile;
    string mOutputDumpFile;

    PerformanceStatic* mOsdPerformance;
};
#endif  //_OSD_ELEMENT_H__
