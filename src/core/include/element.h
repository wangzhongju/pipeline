#ifndef _ESSDK_PL_CELEMENT_H__
#define _ESSDK_PL_CELEMENT_H__

#include <numa.h>
#include <numaif.h>
#include <sched.h>
#include <semaphore.h>
#include <stdarg.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <cassert>
#include <functional>
#include <thread>
#include <vector>

#include "defines.h"
#include "error.h"
#include "log.h"
#include "perf_tools.h"
#include "queue.h"
#include "sw_performance.h"
#include "utilities.h"

enum ELEMENT_PERF_TYPE { SYNC_PERF_ELEMENT, ASYNC_PERF_ELEMENT, IGNORE_PERF_ELEMENT };

enum BASE_ELEMENT_TYPE {
    VIDEO_DECODER = 0,
    AUDIO_DECODER,
    VIDEO_OUTPUT,
    AUDIO_OUTPUT,
};

enum BASE_QUERY_DIRECTION {
    PREV_ELEMENT = 0,
    NEXT_ELEMENT,
};

typedef struct {
    std::string path;
    std::string name;
    int timeout = 0;
    int loopnum = 1;
    int fps = 0;
    int depth = 5;
    int type = 0;
    int maxFrameRate = 0;
    uint timeWnd = 0;
    int dieIndex = 0;
    int startPadIndex = 0;
    int muxPoolSize = 6;
    int ipcCapacity = 0;
    BASE_ELEMENT_TYPE elementType = VIDEO_DECODER;
} ElementParam;

class CElement;
// 用于在指定 NUMA 节点分配内存并构造对象
inline void *bindNumaNode(int nodeId, size_t size) {
    void *ptr = NULL;

    if (numa_num_configured_nodes() <= 1) {
        try {
            ptr = ::operator new(size);
        } catch (const std::bad_alloc &e) {
            std::cerr << "Failed to allocate memory using new: " << e.what() << std::endl;
            exit(1);
        }
    } else {
        ptr = numa_alloc_onnode(size, nodeId);
        if (ptr == nullptr) {
            std::cerr << "Failed to allocate memory on NUMA node " << nodeId << std::endl;
            exit(1);
        }
    }

    return ptr;
}

inline void freeNumaNode(void *ptr, size_t size) {
    if (ptr == nullptr) {
        return;  // 空指针无需处理
    }

    // 根据 NUMA 节点数量选择释放方式
    if (numa_num_configured_nodes() <= 1) {
        ::operator delete(ptr);  // 标准释放
    } else {
        numa_free(ptr, size);  // NUMA 释放（需传入大小）
    }

    ptr = nullptr;
}

inline void set_thread_priority(int priority) {
    struct sched_param param;
    param.sched_priority = priority;

    pthread_t thread = pthread_self();

    // Set scheduling policy and priority (using SCHED_FIFO or SCHED_RR)
    if (pthread_setschedparam(thread, SCHED_FIFO, &param) != 0) {
        perror("Failed to set thread priority");
    } else {
        printf("Thread priority set to %d\n", priority);
    }
}

// 绑定线程在某个numa上运行
inline void set_thread_affinity(int numa_node) {
    // 获取指定 NUMA 节点的 CPU 集合
    struct bitmask *cpus = numa_allocate_cpumask();
    numa_node_to_cpus(numa_node, cpus);

    // 设置 CPU 亲和性
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    for (int i = 0; i < cpus->size; ++i) {
        if (numa_bitmask_isbitset(cpus, i)) {
            CPU_SET(i, &cpuset);
        }
    }

    // 应用 CPU 亲和性设置
    pthread_t thread = pthread_self();
    if (pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) != 0) {
        std::cerr << "Failed to set thread affinity\n";
    }

    numa_free_cpumask(cpus);
}

class CElement {
   public:
    CElement(const char *name = "element", const char *config = "", int dieIndex = 0,
             BASE_ELEMENT_TYPE elementType = VIDEO_DECODER)
        : mName(name), m_configFile(config), m_dieIndex(dieIndex), mElementType(elementType) {
        m_VBName = dieIndex == 0 ? "mmz_nid_0_part_0" : "mmz_nid_1_part_0";
        m_cpuID = sched_getcpu();
        getCpuNumaID("constract");
    }
    CElement(const ElementParam *param) {
        mName = param->name;
        m_configFile = param->path;
        m_dieIndex = param->dieIndex;
        mElementType = param->elementType;
        m_VBName = m_dieIndex == 0 ? "mmz_nid_0_part_0" : "mmz_nid_1_part_0";
        m_cpuID = sched_getcpu();
        getCpuNumaID("constract");
    }
    virtual ~CElement() = default;

    static int getPadIndex() {
        static int nextId = 0;  // 静态局部变量，保证线程安全（C++11 起）
        return nextId++;
    }

    // 插件的初始化接口，所有插件除了线程启动外，所有初始化动作都在此接口实现；
    virtual app_ret Init() { return APP_SUCCESS; }

    // 启动插件的线程，主要针对src和queue插件的线程启动；
    // 设计init和start两个接口的主要原因是，所有插件Add进pipeline的次序不一致，如果所有的准备动作都放在同一个接口中的话,
    // 会引发有src插件都已经运行好多帧数据了，都下发到某个插件的时候，这个插件的准备工作还没做好。比如，帧数据已经到推理插件了，但是推理插件的模型加载还没完成；
    virtual app_ret Start() { return APP_SUCCESS; }

    // 处理本插件数据；是一个纯虚函数，需要各个插件自己来实现；
    virtual app_ret ProcessData(CBaseMeta *baseMeta, CElement const *previousElement = 0) = 0;

    // 将数据传递给下一个插件，并调用下一个插件的数据处理接口；
    app_ret TransMitToNextToProcess(CBaseMeta *baseMeta) {
        // 下个插件处理数据；
        app_ret ret;
        for (auto element : m_NextElementVec) {
            ret = element->ProcessAndTransmit(baseMeta, this);
            if (APP_SUCCESS != ret) {
                return ret;
            }
        }
        return APP_SUCCESS;
    };

    // 统一调用接口，方便在基类中统计性能指标；
    //  ProcessAndTransmit接口是统一调用接口，实现这个接口的主要原因是方便一些统计性的功能,能在基类中统一实现，不需要各个子类自己重复写代码；
    // 这个接口可以被子类覆写，典型的比如queue插件，他的ProcessAndTransmit只能调用ProcessData接口，然后是另起一个线程调用TransMitToNextToProcess接口；
    virtual app_ret ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *self = 0) {
        // wait for start function finished
        if (!mRunningFlag) {
            sem_wait(&mStartFlag);
            mRunningFlag = true;
        }

        ProcessData(baseMeta, this);

        TransMitToNextToProcess(baseMeta);
        return APP_SUCCESS;
    }

    // 等待有线程的子类调用结束，该接口主要给pipeline类使用；如果没有线程，则不需要覆写此接口；
    virtual app_ret Wait() { return APP_SUCCESS; }

    // 释放资源；
    virtual app_ret Finish() { return APP_SUCCESS; }

    virtual app_ret perfStat() {
        // app_debug("%s has nothing to do with performance.\n", mName.c_str());
        return APP_SUCCESS;
    }

    bool dumpFile(std::string fileName, char *buf, uint64_t bufSize, string className = "",
                  string root = "/tmp/essdkpl/") {
        if (className.empty()) {
            className = mName;
        }
        std::string filePath = root + className + "/" + fileName;
        app_debug("filepath %s\n", filePath.c_str());
        assert(createDir(filePath) == APP_SUCCESS);

        FILE *saveFile = fopen(filePath.c_str(), "wb");
        if (NULL == saveFile) {
            app_debug("%s-%d: %s \n", __func__, __LINE__, "fopen error");
            return false;
        }

        int count = fwrite(buf, sizeof(char), bufSize, saveFile);
        fclose(saveFile);

        if (count != bufSize) {
            return false;
        }

        return true;
    };

    virtual app_ret InfoQuery(void *data, BASE_QUERY_TYPE type, BASE_QUERY_DIRECTION direction, int padIndex,
                              CElement *inquirerElement) {
        app_ret ret = APP_FAILURE;
        if (direction == PREV_ELEMENT) {
            int num = m_PreviousElementVec.size();
            if (num == 1) {
                ret = m_PreviousElementVec[0]->InfoQuery(data, type, direction, padIndex, this);
            } else if (padIndex >= 0 && padIndex < num) {
                ret = m_PreviousElementVec[padIndex]->InfoQuery(data, type, direction, padIndex, this);
            } else {
                ret = APP_FAILURE;
            }
        } else if (direction == NEXT_ELEMENT) {
            int num = m_NextElementVec.size();
            if (num == 1) {
                ret = m_NextElementVec[0]->InfoQuery(data, type, direction, padIndex, this);
            } else if (padIndex >= 0 && padIndex < num) {
                ret = m_NextElementVec[padIndex]->InfoQuery(data, type, direction, padIndex, this);
            } else {
                ret = APP_FAILURE;
            }
        }
        return ret;
    };

   public:
    string mName;  // 插件的名字；
    BASE_ELEMENT_TYPE mElementType;
    ELEMENT_PERF_TYPE mPerfType = IGNORE_PERF_ELEMENT;
    string mDumpBasePath = "./dump";

   protected:
    string m_configFile;  // 插件的配置文件路径；
    std::vector<CElement *>
        m_PreviousElementVec;  // 和本插件直接link的所有上游插件，如果是多对一插件，则此变量的size为link的插件个数；
    std::vector<CElement *>
        m_NextElementVec;  // 和本插件直接link的所有下游插件，如果是一对多插件，则此变量的size为link的插件个数；

    std::string dumpPath;
    friend class CPipeLine;

    sem_t mStartFlag;
    bool mRunningFlag = false;
    PerformanceStatic *processPerformance = nullptr;
    // bool mIsAudio;  //是否音频类插件，构造函数设默认值false，demux插件使用
   public:
    int m_dieIndex;
    std::string m_VBName;

   public:
    void get_numa_node(void *addr) {
        int nodeClass = -1;
        move_pages(0, 1, (void **)&addr, NULL, &nodeClass, 0);

        int nodeLocal = -1;
        std::string *Ptemp = &mName;
        move_pages(0, 1, (void **)&Ptemp, NULL, &nodeLocal, 0);

        printf("the element %s on the die id is %d ; the local value on die %d ;\n", mName.c_str(), nodeClass,
               nodeLocal);

        return;
    };
    int m_cpuID;
    bool m_cpuSetFlag = false;

    int getCpuNumaID(const char *func) {
        int cpu = sched_getcpu();
        printf("the element %s : Function %s run on the cpu %d \n", mName.c_str(), func, cpu);
        return cpu;
    };
};
#endif  //_ESSDK_PL_CELEMENT_H__
