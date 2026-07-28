#ifndef __SW_PERFORMANCE_H__
#define __SW_PERFORMANCE_H__

// #include "basetype.h"
#ifdef __linux
#include <sys/syscall.h>
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

#include <mutex>
#include <shared_mutex>
#include <string>

#include "forward.h"

using namespace std;
#define RECORD_MAX 5000
unsigned long long esclock();

enum PERF_STATIC_FLAG { PERF_STATIC_NO = 0, PERF_STATIC_STDOUT, PERF_STATIC_FLIE };
extern enum PERF_STATIC_FLAG gperfStatisFlag;

enum PERF_STATIC_MODE { PERF_STATIC_CONTINUOUS = 0, PERF_STATIC_SEGMENT };

extern PerformanceStatic *frameReleasePerformance;
extern PerformanceStatic *preMetaReleasePerformance;
extern PerformanceStatic *npuMetaReleasePerformance;
extern PerformanceStatic *rawImgMetaReleasePerformance;
extern PerformanceStatic *objMetaReleasePerformance;

class PerformanceStatic {
   public:
    PerformanceStatic(string name = "", PERF_STATIC_MODE mode = PERF_STATIC_CONTINUOUS)
        : mCount(0), mTotal(0), mStart(0), mEnd(0), need_continue(0), need_end(0), inCount(0), outCount(0) {
        mName = name;
        memset(mValue, 0, sizeof(mValue));

        mMode = mode;

        char *levelvar = getenv("PERF_STATIC_FLAG");
        if (levelvar) {
            int tmp = (int)atoi(levelvar);
            if (tmp == 0) {
                gperfStatisFlag = PERF_STATIC_NO;
            } else if (tmp == 1) {
                gperfStatisFlag = PERF_STATIC_STDOUT;
            } else if (tmp == 2) {
                gperfStatisFlag = PERF_STATIC_FLIE;
            }
        }
        if (gperfStatisFlag == PERF_STATIC_FLIE) {
            dumpFp = NULL;
            string tmp = mName + "_dump.log";
            dumpFp = fopen(tmp.c_str(), "wb");
        }
    };
    ~PerformanceStatic() {
        if ((gperfStatisFlag == PERF_STATIC_FLIE) && dumpFp) fclose(dumpFp);
    };
    void performanceStaticStart();
    void performanceStaticEnd(int loopnum = 1);
    void dataInCount(unsigned int cnt = 1);
    void dataOutCount(unsigned int cnt = 1);
    void performanceStaticReport();
    void performanceStaticReport(string privStr);
    void performanceStaticReportCount();
    static void setPeriodTime(int _period) { periodTime = _period; }

   public:
    string mName;
    unsigned int mCount;
    unsigned long long mTotal;
    unsigned int mValue[RECORD_MAX];
    unsigned long long mStart;
    unsigned long long mEnd;
    int need_continue;
    int need_end;
    unsigned int inCount;
    unsigned int outCount;
    static int periodTime;

   private:
    std::mutex mtx;
    PERF_STATIC_MODE mMode;
    FILE *dumpFp;
};

#endif /* __SW_PERFORMANCE_H__ */
