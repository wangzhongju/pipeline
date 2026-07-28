#define PL_LOG_ID PL_LOG_CORE
#include "sw_performance.h"

#include <fstream>
#include <iostream>
enum PERF_STATIC_FLAG gperfStatisFlag = PERF_STATIC_NO;

#ifdef SUPPORT_SW_CYCLE_PERF
static inline unsigned long long metal_timer_get_cyclecount(unsigned long long *mcc) {
    unsigned long long cycles;
    __asm__ volatile("rdtime %0" : "=r"(cycles));
    *mcc = cycles;

    return cycles;
}
#endif

#ifdef SUPPORT_SW_CYCLE_PERF
unsigned long long esclock() {
    unsigned long long mcc;
    metal_timer_get_cyclecount(&mcc);
    return mcc;
}
#else
unsigned long long esclock() {
    struct timeval tmp;
    unsigned long long mcc = 0;
    gettimeofday(&tmp, NULL);
    mcc = tmp.tv_sec * 1000000 + tmp.tv_usec;
    return mcc;
}
#endif

int PerformanceStatic::periodTime = 300000;

void PerformanceStatic::performanceStaticStart() {
    if (gperfStatisFlag == PERF_STATIC_NO) {
        return;
    }
    std::lock_guard<std::mutex> lock(mtx);
    if (!need_continue) {
        // metal_timer_get_cyclecount(&name##_start);
        mStart = esclock();
        need_continue = 1;
        need_end = 1;
    } else {
        if (0) {
            printf("[%s(%d)] continue\n", __FILE__, __LINE__);
            fflush(stdout);
        }
    }
}

void PerformanceStatic::performanceStaticEnd(int loopnum) {
    if (gperfStatisFlag == PERF_STATIC_NO) {
        return;
    }
    std::lock_guard<std::mutex> lock(mtx);
    mEnd = esclock();
    need_continue = 0;
    if (0) {
        printf("[%s(%d)] need_continue is %d \n", __FILE__, __LINE__, need_continue);
        fflush(stdout);
    }
    if (need_end) {
        if (0) {
            printf("[%s(%d)] %d end\n", __FILE__, __LINE__, mCount);
            fflush(stdout);
        }
        unsigned int value = (mEnd - mStart);
        if (mCount < RECORD_MAX) mValue[mCount] = value / (loopnum > 0 ? loopnum : 1);
        mCount++;
        mTotal += value;
        need_end = 0;
    } else {
        if (0) {
            printf("[%s(%d)]\n", __FILE__, __LINE__);
            fflush(stdout);
        }
    }
}

void PerformanceStatic::dataInCount(unsigned int cnt) {
    std::lock_guard<std::mutex> lock(mtx);
    inCount += cnt;
}

void PerformanceStatic::dataOutCount(unsigned int cnt) {
    std::lock_guard<std::mutex> lock(mtx);
    outCount += cnt;
}

void PerformanceStatic::performanceStaticReport() {
    if (gperfStatisFlag == PERF_STATIC_NO) {
        return;
    }

    std::lock_guard<std::mutex> lock(mtx);
    if (gperfStatisFlag == PERF_STATIC_FLIE) {
        // printf("dumpFp=%p\n",dumpFp);
        fprintf(dumpFp,
                "\n name: %s \n count: %d, total: %lld us, %lld us pertime, %lld "
                "fps, periodTime %d ms\n",
                mName.c_str(), mCount, mTotal, mCount ? mTotal / mCount : 0, mTotal ? mCount * 1000000 / mTotal : 0,
                int(periodTime / 1000));
        fflush(dumpFp);
    } else {
        printf(
            "\n name: %s \n count: %d, total: %lld us, %lld us pertime, %lld "
            "fps, "
            "periodTime %d ms\n",
            mName.c_str(), mCount, mTotal, mCount ? mTotal / mCount : 0, mTotal ? mCount * 1000000 / mTotal : 0,
            int(periodTime / 1000));
        fflush(stdout);
    }

    if (1) {
        char tmp[1000000];
        int stringLen = 0;
        for (int i = 0; i < ((mCount < RECORD_MAX) ? mCount : RECORD_MAX); i++) {
            stringLen += sprintf(tmp + stringLen, "%d ", mValue[i]);
            if (i % 10 == 9) stringLen += sprintf(tmp + stringLen, "\n");
        }
        tmp[stringLen] = 0;
        if (gperfStatisFlag == PERF_STATIC_FLIE) {
            fprintf(dumpFp, "%s", tmp);
            fflush(dumpFp);
        } else {
            printf("%s", tmp);
            fflush(stdout);
        }
    }
    if (mMode == PERF_STATIC_SEGMENT) {
        mCount = 0;
        mTotal = 0;
        memset(mValue, 0, sizeof(mValue));
    }
}

void PerformanceStatic::performanceStaticReport(string privStr) {
    if (gperfStatisFlag == PERF_STATIC_NO) {
        return;
    }
    std::lock_guard<std::mutex> lock(mtx);
    if (gperfStatisFlag == PERF_STATIC_FLIE) {
        fprintf(dumpFp, "%s\n", privStr.c_str());
        fflush(dumpFp);
    } else {
        printf("%s\n", privStr.c_str());
        fflush(stdout);
    }
}

void PerformanceStatic::performanceStaticReportCount() {
    if (gperfStatisFlag == PERF_STATIC_NO) {
        return;
    }
    std::lock_guard<std::mutex> lock(mtx);
    if (gperfStatisFlag == PERF_STATIC_FLIE) {
        fprintf(dumpFp,
                "\n name: %s \n incount: %u, outcount: %u, queue depth: %d, "
                "periodTime %d ms\n",
                mName.c_str(), inCount, outCount, (int)inCount - outCount, int(periodTime / 1000));
        fflush(dumpFp);
    } else {
        printf(
            "\n name: %s \n incount: %u, outcount: %u, queue depth: %d, "
            "periodTime %d ms\n",
            mName.c_str(), inCount, outCount, (int)inCount - outCount, int(periodTime / 1000));
        fflush(stdout);
    }
    fflush(stdout);
}