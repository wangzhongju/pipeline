#define PL_LOG_ID PL_LOG_CORE
#include "pipeline.h"

#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <stdarg.h>
extern "C" {
#include "es_vb_memory.h"
}

#define PL_COMMON_VB_BUF_DEFAULT_CNT 10
#define PL_COMMON_VB_BUF_DEFAULT_SIZE 4096
#define SEM_NAME "/essdkpl_core_semaphore"

IpcDecodeCnt gIpcDecCnt = {0};
uint64_t gVoCount = 0;
volatile bool isAllElementStart = false;
volatile bool isVoEosFlag = false;
PerformanceStatic *frameReleasePerformance = new PerformanceStatic("frameMetaRelease_Performance", PERF_STATIC_SEGMENT);
PerformanceStatic *preMetaReleasePerformance = new PerformanceStatic("preMetaRelease_Performance", PERF_STATIC_SEGMENT);
PerformanceStatic *npuMetaReleasePerformance = new PerformanceStatic("npuMetaRelease_Performance", PERF_STATIC_SEGMENT);

PerformanceStatic *rawImgMetaReleasePerformance =
    new PerformanceStatic("rawImgRelease_Performance", PERF_STATIC_SEGMENT);
PerformanceStatic *objMetaReleasePerformance = new PerformanceStatic("objMetaRelease_Performance", PERF_STATIC_SEGMENT);

CPipeLine::CPipeLine(string name, bool flushFlag) {
    if (flushFlag) {
        setbuf(stdout, NULL);
    }
    mStatInterval = 1000000;
}

app_ret CPipeLine::AddToPipeline(CElement *ele1, ...) {
    app_info("%s \n", "Enter");
    if (NULL != ele1) {
        mElements.push_back(ele1);
    } else {
        return APP_SUCCESS;
    }

    va_list arguments;
    va_start(arguments, ele1);
    while (1) {
        CElement *ele = va_arg(arguments, CElement *);
        if (NULL == ele) {
            break;
        }
        mElements.push_back(ele);
    }

    va_end(arguments);

    return APP_SUCCESS;
}

app_ret CPipeLine::LinkMany(CElement *ele1, ...) {
    app_info(" %s \n", "Enter");
    if (NULL != ele1) {
        va_list arguments;
        va_start(arguments, ele1);
        while (1) {
            CElement *eleNext = va_arg(arguments, CElement *);
            if (NULL == eleNext) {
                break;
            }
            ele1->m_NextElementVec.push_back(eleNext);
            eleNext->m_PreviousElementVec.push_back(ele1);
            ele1 = eleNext;
        }

        va_end(arguments);
    }

    return APP_SUCCESS;
}

app_ret CPipeLine::Init() {
    LOGPL_Init("pl_log.conf");
    app_info(" %s \n", "Enter");
#if LOG_ENABLED
    set_env_log_level();
    ES_CHAR *cfgPath = ES_NULL;
    cfgPath = getenv("essdk_log_config_path");
    ES_SYS_SetLogCfgPath(cfgPath);
#endif
    // SYS_LOGLEVEL_E sysLogLevel = SYS_LOGLEVEL_DEBUG;

    // #if LOG_ENABLED
    //     set_env_log_level();
    //     if(gEnvLogLevel > PL_LOG__NOTICE)
    //         sysLogLevel = SYS_LOGLEVEL_WARN;
    // #else
    //     sysLogLevel = SYS_LOGLEVEL_OFF;
    // #endif
    //     ES_SYS_SetLogLevel(&sysLogLevel);

    ES_S32 ret;
    VB_CONFIG_S vbConfig = {0};

    app_debug(" %s \n", "pipeline init start");
    sem_t *sem = sem_open(SEM_NAME, O_CREAT | O_EXCL, 0644, 1);
    if (sem == SEM_FAILED) {
        if (errno == EEXIST) {
            // 信号量已存在，说明初始化已经被其他进程执行
            printf("ES_VB_SetConfig already performed by another process\n");

            ret = ES_VB_GetConfig(&vbConfig);
            printf("ES_VB_GetConfig return %d\n", ret);
            if (ret == ES_SUCCESS) {
                printf("ES_VB_SetConfig has been configed\n");
            }

            // // 打开已存在的信号量以便后续使用（如果需要）
            // sem = sem_open(SEM_NAME, 0);
            // if (sem == SEM_FAILED) {
            //     perror("sem_open (existing semaphore)");
            //     exit(EXIT_FAILURE);
            // }
        } else {
            perror("sem_open!!!!");
            exit(EXIT_FAILURE);
        }
    } else {
        // 信号量成功创建，执行初始化,the first process
        vbConfig.poolCnt = 1;
        vbConfig.poolCfgs[0].blkCnt = PL_COMMON_VB_BUF_DEFAULT_CNT;
        vbConfig.poolCfgs[0].blkSize = PL_COMMON_VB_BUF_DEFAULT_SIZE;
        app_debug(" %s \n", "ES_VB_SetConfig start");
        ES_S32 ret2 = ES_VB_SetConfig(&vbConfig);
        app_debug(" %s \n", "ES_VB_Init start");
        ret2 = ES_VB_Init();
        printf("ES_VB_Init return %d\n", ret2);
    }

    // 释放（关闭）信号量
    if (sem_close(sem) == -1) {
        perror("sem_close");
    }

    app_debug(" %s \n", "ES_SYS_Init start");
    ES_SYS_Init();
    app_debug(" %s \n", "ES_SYS_Init end");
    for (auto element : mElements) {
        element->get_numa_node(element);
    }
    for (auto element : mElements) {
        app_ret ret = APP_SUCCESS;
        app_info(" element init:%s \n", element->mName.c_str());
        ret = element->Init();
        if (ret != APP_SUCCESS) {
            app_error(" %s %s\n", " Init fail, the element is : ", element->mName.c_str());
            return ret;
        }
        sem_init(&element->mStartFlag, 0, 0);
    }

    app_info(" %s \n", "Exit");
    return APP_SUCCESS;
}

void CPipeLine::setDieAffinety(int die) { set_thread_affinity(die); }

app_ret CPipeLine::Start() {
    app_info(" %s \n", "Enter");
    for (auto element : mElements) {
        app_info(" element Start :%s\n", element->mName.c_str());
        const app_ret ret = element->Start();
        if (ret != APP_SUCCESS) {
            app_error(" Start fail, the element is: %s ret: 0x%x\n",
                      element->mName.c_str(), ret);
            return ret;
        }
        sem_post(&element->mStartFlag);
    }
    isAllElementStart = true;
    perfStatRun = true;
    if (PERF_STATIC_NO == gperfStatisFlag) {
        perfStatRun = false;
    }
    kpiTid = std::thread(std::mem_fn(&CPipeLine::PerfStatisticsTimer), this);
    app_info(" %s \n", "Exit");

    return APP_SUCCESS;
};

app_ret CPipeLine::WaitForFinish() {
    app_info(" %s \n", "Enter");

    for (auto element : mElements) {
        app_info("WaitForFinish element Wait :%s\n", element->mName.c_str());
        element->Wait();
        app_info("WaitForFinish element Wait end:%s\n", element->mName.c_str());
    }
    perfStatRun = false;
    kpiTid.join();
    app_info(" %s \n", "Exit");
    return APP_SUCCESS;
};

void CPipeLine::PerfStatisticsTimer() {
    unsigned long long statCost = 0, t1 = 0, t2 = 0;
    prctl(PR_SET_NAME, "PerfStati");
    while (perfStatRun) {
        int actual_sleep = mStatInterval - statCost;
        if (actual_sleep > 0) {
            usleep(actual_sleep);
        } else {
            app_warn("statCost is larger than sleep time. \n");
        }

        t1 = esclock();
        for (auto element : mElements) {
            element->perfStat();
        }

        t2 = esclock();
        statCost = t2 - t1;
    }

    return;
}

app_ret CPipeLine::SetStatInterval(int statInterval) {
    mStatInterval = statInterval;
    PerformanceStatic::setPeriodTime(mStatInterval);
    return APP_SUCCESS;
}

app_ret CPipeLine::Finish() {
    app_info(" %s \n", "Enter");
    for (auto element : mElements) {
        app_info(" element Finish:%s \n", element->mName.c_str());
        element->Finish();
        sem_destroy(&element->mStartFlag);
    }
    app_debug(" %s \n", "element finish end, will start ES_SYS_Exit ");
    ES_SYS_Exit();

    app_info(" %s \n", "Exit");
    return APP_SUCCESS;
}

app_ret CPipeLine::notifyExit() {
    app_debug(" %s \n", "pipeline will wait for Start() and set EOS to Exit ");
    isVoEosFlag = true;
    return APP_SUCCESS;
}
