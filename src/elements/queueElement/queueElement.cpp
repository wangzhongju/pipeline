#define PL_LOG_ID PL_LOG_QUEUE
#include "queueElement.h"

void QueueElement::threadFunc() {
    prctl(PR_SET_NAME, mName.c_str());
    if (!m_cpuSetFlag) {
        // cpu_set_t cpuset;
        // CPU_ZERO(&cpuset);
        // CPU_SET(m_cpuID, &cpuset);
        // pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        // m_cpuSetFlag = true;
        set_thread_affinity(m_dieIndex);
        getCpuNumaID(__func__);
    }

    while (true) {
        CBaseMeta *baseMeta = m_queueData.pop();
        bool exitFlag = baseMeta->eosFlag;
        app_debug("%s size %d\n", mName.c_str(), m_queueData.size());
        m_theRealDepth = m_queueData.size();
        auto start = std::chrono::high_resolution_clock::now();

        TransMitToNextToProcess(baseMeta);

        auto end = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> elapsed = end - start;
        app_debug("the queue %s process all time took %f ms\n", mName.c_str(), elapsed.count());

        if (exitFlag) {
            printf("ElementInner eos  %s \n", mName.c_str());
            break;
        }
    }
}

app_ret QueueElement::Init() {
    if (m_PreviousElementVec.size() != 1 || m_NextElementVec.size() != 1) {
        return APP_FAILURE;
    }
    return APP_SUCCESS;
}

app_ret QueueElement::Start() {
    m_queueThread = std::thread(std::mem_fn(&QueueElement::threadFunc), this);
    return APP_SUCCESS;
}

app_ret QueueElement::Wait() {
    m_queueThread.join();
    return APP_SUCCESS;
}

app_ret QueueElement::ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) {
    app_debug("%s in\n", mName.c_str());
    if (1 == queueType)  // 丢帧模式；
    {
        if (m_queueData.full()) {
            m_queueData.pop()->reduceUseCount();
        }
    }
    m_queueData.push_back(baseMeta);
    app_debug("%s out size %d \n", mName.c_str(), m_queueData.size());
    return APP_SUCCESS;
}

app_ret QueueElement::Finish() {
    freeNumaNode(this, sizeof(QueueElement));
    return APP_SUCCESS;
}

app_ret QueueElement::ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *previousElement) {
    ProcessData(baseMeta, this);
    return APP_SUCCESS;
}

extern "C" CElement *createEsQueueElement(int depth, int type, const char *name, int dieIndex) {
    return new (bindNumaNode(dieIndex, sizeof(QueueElement))) QueueElement(depth, type, name, "", dieIndex);
}
