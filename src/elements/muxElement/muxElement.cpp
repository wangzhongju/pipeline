#define PL_LOG_ID PL_LOG_SMUX
#include "muxElement.h"

#include "pipeline.h"

static atomic<uint64_t> gVdecGetCount = 0;
static atomic<uint64_t> gVdecReleaseCount = 0;
static atomic<uint64_t> gBatchIndexCount = 0;

static PerformanceStatic *pMuxPerformace;

template <class T>
muxBlockQueue<T>::~muxBlockQueue() {
    {
        std::lock_guard<std::mutex> locker(mtx);
        deq.clear();
    }
    Producer.notify_all();
    Consumer.notify_all();
}

template <class T>
void muxBlockQueue<T>::push_back(const T &item) {
    //使用条件变量前应先对互斥量上锁，此时选择unique_lock而非lock_guard
    //因为unique_lock在上锁后允许临时解锁再加锁，而lock_guard上锁后只能在离开作用域时解锁
    std::unique_lock<std::mutex> lock(mtx);
    while (deq.size() >= capacity) {
        //若队列已满，则生产者线程进入阻塞状态，自动对互斥量解锁，被唤醒后又自动对互斥量加锁
        Producer.wait(lock);
    }
    deq.push_back(item);    //向队列中放入数据
    Consumer.notify_one();  //唤醒一个阻塞的消费者线程
}

template <class T>
bool muxBlockQueue<T>::pop(T &item) {
    std::unique_lock<std::mutex> lock(mtx);
    while (deq.empty()) {
        //若队列空，则消费者线程进入阻塞，等待被唤醒
        Consumer.wait(lock);
    }
    item = deq.front();  //当作删除数据的拷贝
    deq.pop_front();
    Producer.notify_one();  //唤醒一个阻塞的生产者线程
    return true;
}

template <class T>
bool muxBlockQueue<T>::pop(T &item, int timeout) {
    std::unique_lock<std::mutex> lock(mtx);
    while (deq.empty()) {
        //阻塞时间超过指定的timeout，直接返回false
        if (Consumer.wait_for(lock, std::chrono::milliseconds(timeout)) == std::cv_status::timeout) return false;
    }
    item = deq.front();
    deq.pop_front();
    Producer.notify_one();
    return true;
}

template <class T>
bool muxBlockQueue<T>::empty() {
    std::lock_guard<std::mutex> lock(mtx);
    return deq.empty();
}

template <class T>
bool muxBlockQueue<T>::full() {
    std::lock_guard<std::mutex> lock(mtx);
    return deq.size() >= capacity;
}

template <class T>
uint64_t muxBlockQueue<T>::size() {
    std::lock_guard<std::mutex> lock(mtx);
    return deq.size();
}

extern "C" CElement *createEsMuxElement(int waitTime, const char *name, int muxPoolSize, int dieIndex) {
    return new (bindNumaNode(dieIndex, sizeof(MuxElement))) MuxElement(waitTime, name, muxPoolSize, dieIndex);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

app_ret MuxElement::Init() {
    if (m_PreviousElementVec.size() < 1 || m_NextElementVec.size() != 1) {
        return APP_FAILURE;
    }
    pMuxPerformace = new PerformanceStatic(mName + "_muxGetFramePerformace", PERF_STATIC_SEGMENT);
    bmetaPool = new MetaPool<CBatchMeta>(muxPSize, m_dieIndex, "smux");
    for (int i = 0; i < MAX_VIDEO_GRP_NUM; i++) {
        m_elementMapFrameMeta[i] = NULL;
        m_elementEosFlag[i] = false;
    }
    app_debug("\n muxPSize %d \n", muxPSize);
    app_debug("%s %s %s %ld %s\n", "the muxElement name is : ", mName.c_str(), ", the waitTime is ", m_waitTimeMs,
              "ms");
    return APP_SUCCESS;
}

app_ret MuxElement::perfStat() {
    pMuxPerformace->performanceStaticReport();
    return APP_SUCCESS;
}

app_ret MuxElement::Start() {
    for (uint index = 0; index < m_PreviousElementVec.size(); index++) {
        // m_elementIndex[m_PreviousElementVec[index]] = index;
        // m_elementEosFlag[m_PreviousElementVec[index]] = false;
        m_frameMetaIndexVec.push_back(0);
    }
    int ipcCount = gIpcDecCnt.ipcCnt;
    for (uint tmpIndex = 0; tmpIndex < ipcCount; tmpIndex++) {
        m_ipcIndexVec.push_back(gIpcDecCnt.ipcIndexs[tmpIndex]);
        m_ipcPopMap.insert(std::map<int, int>::value_type(gIpcDecCnt.ipcIndexs[tmpIndex], 0));
        m_ipcReduceMap.insert(std::map<int, int>::value_type(gIpcDecCnt.ipcIndexs[tmpIndex], 0));
    }

    for (uint index = 0; index < m_PreviousElementVec.size(); index++) {
        m_elementMapFrameMeta[index] = new muxBlockQueue<CFrameMeta *>(10);
        m_elementIndexMap[m_PreviousElementVec[index]] = index;
    }

    m_transmitThread = std::thread(std::mem_fn(&MuxElement::transToNext), this);
    m_startTime = esclock() / 1000;
    app_debug("%s %ld\n", " mux startTime is :", m_startTime);
    return APP_SUCCESS;
}

app_ret MuxElement::ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *previousElement) {
    return ProcessData(baseMeta, previousElement);
}

app_ret MuxElement::ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) {
    app_debug("%s in\n", PLLOG_fileName(__FILE__), __func__, __LINE__, previousElement->mName.c_str());
    if (baseMeta->mMetaType != FRAME_META) {
        app_error("%s\n", "the muxElement receive a meta not frame meta");
        return APP_FAILURE;
    }
    CFrameMeta *frameMeta = (CFrameMeta *)(baseMeta);
    app_ret retVal = APP_SUCCESS;

    const CElement *preElement = previousElement;

    int tempIndex = m_elementIndexMap[preElement];
    // m_elementMapFrameMeta[frameMeta->padIndex]->push_back(frameMeta);
    m_elementMapFrameMeta[tempIndex]->push_back(frameMeta);

    bool all_ok = true;
    for (size_t i = 0; i < m_PreviousElementVec.size(); ++i) {
        if (m_elementMapFrameMeta[i]->empty()) {
            all_ok = false;
            break;
        }
    }
    if (all_ok) {
        notify.notify();
    }

    app_debug("%s padIndex is : %d\n", previousElement->mName.c_str(), frameMeta->padIndex);
    app_debug("%s eosflag is : %d\n", previousElement->mName.c_str(), frameMeta->eosFlag);
    app_debug("%s frameIndex is : %d\n", previousElement->mName.c_str(), frameMeta->index);
    app_debug("%s pts is : %lu\n", previousElement->mName.c_str(), frameMeta->pts);
    app_debug("%s the system time is : %.3f\n", previousElement->mName.c_str(), double(esclock() / 1000));
    app_debug("%s out\n", PLLOG_fileName(__FILE__), __func__, __LINE__, previousElement->mName.c_str());
    return retVal;
}

app_ret MuxElement::Wait() {
    m_transmitThread.join();
    return APP_SUCCESS;
}

app_ret MuxElement::Finish() {
    app_debug("%s\n", "start release map");

    for (int i = 0; i < MAX_VIDEO_GRP_NUM; i++) {
        if (m_elementMapFrameMeta[i]) {
            delete m_elementMapFrameMeta[i];
            m_elementMapFrameMeta[i] = NULL;
        }
    }

    delete bmetaPool;
    delete pMuxPerformace;
    freeNumaNode(this, sizeof(MuxElement));
    app_debug("%s\n", " release map end");
    return APP_SUCCESS;
}

void MuxElement::transToNext() {
    int iPreElementSize = m_PreviousElementVec.size();
    app_debug("%s %d\n", " enter transToNext , the pre element size is : ", iPreElementSize);
    string threadName = mName + "_mux";
    prctl(PR_SET_NAME, (unsigned long)(threadName.c_str()));

    if (!m_cpuSetFlag) {
        // cpu_set_t cpuset;
        // CPU_ZERO(&cpuset);
        // CPU_SET(m_cpuID, &cpuset);
        // pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        set_thread_affinity(m_dieIndex);
        m_cpuSetFlag = true;
    }

    static int times = 0;

    bool haveFrame = false;
    while (!haveFrame) {
        for (size_t i = 0; i < m_PreviousElementVec.size(); ++i) {
            if (!m_elementMapFrameMeta[i]->empty()) {
                haveFrame = true;
                break;
            }
        }
        if (haveFrame) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    while (1) {
        int ok = notify.wait_for(std::chrono::milliseconds(m_waitTimeMs));
        if (ok) {
            app_debug(" notify \n");
        } else {
            app_debug("time out\n");
        }
        CBatchMeta *batchMeta = bmetaPool->allocate();

        batchMeta->mMetaType = BATCH_META;
        batchMeta->pool = bmetaPool;
        batchMeta->dieIndex = m_dieIndex;
        batchMeta->smuxTimeout = m_waitTimeMs;
        int iEofSize = 0;
        for (int i = 0; i < iPreElementSize; i++) {
            if (m_elementEosFlag[i]) {
                iEofSize++;
            }
        }
        if (iEofSize == iPreElementSize) {
            batchMeta->batchIndex = m_batchIndex;
            m_batchIndex++;
            batchMeta->eosFlag = true;
            TransMitToNextToProcess(batchMeta);
            gBatchIndexCount++;
            break;
        }

        times++;
        app_debug("MuxElement::transToNext times %d \n", times);

        {
            for (uint index = 0; index < iPreElementSize; index++) {
                CElement const *nowElement = m_PreviousElementVec[index];
                if (m_elementEosFlag[index]) {
                    app_debug("%s %d\n", "the element stream is eos, the index is : ", index);
                    // iEofSize++;
                    continue;
                }

                app_debug("%s %s %s %ld\n", " the element is : ", nowElement->mName.c_str(),
                          " the queue size is : ", m_elementMapFrameMeta[index]->size());

                {
                    CFrameMeta *fmeta = NULL;  // plGetFrame(index, 0);
                    if (!m_elementMapFrameMeta[index]->pop(fmeta, 0)) {
                        if (std::find(m_ipcIndexVec.begin(), m_ipcIndexVec.end(), index) != m_ipcIndexVec.end()) {
                            m_ipcPopMap[index]++;
                        }
                        // app_error("%s %d\n", "the IPC cannot get first
                        // frameMeta : ", index);
                        continue;
                    }

                    fmeta->index = m_frameMetaIndexVec[index];

                    if (fmeta->eosFlag) {
                        m_elementEosFlag[index] = fmeta->eosFlag;
                        app_debug("%s %s\n",
                                  "the frameMeta is end of stream, the element is : ", nowElement->mName.c_str());
                        fmeta->reduceUseCount();

                        continue;
                    }
                    m_frameMetaIndexVec[index]++;
                    ES_ASSERT(fmeta != NULL, "the frameMeta is null : %d", index);
                    batchMeta->addFrameMeta(fmeta);

                    app_debug("%d %lld %lld\n", index, gIpcDecCnt.ipcToDecodingCount[index],
                              gIpcDecCnt.ipcDecodedCount[index]);

                    auto it = std::find(m_ipcIndexVec.begin(), m_ipcIndexVec.end(), index);
                    if (it != m_ipcIndexVec.end()) {
                        if (gIpcDecCnt.ipcToDecodingCount[index] - gIpcDecCnt.ipcDecodedCount[index] > 5) {
                            CFrameMeta *fmeta2 = NULL;  // plGetFrame(index, 0);
                            if (m_elementMapFrameMeta[index]->pop(fmeta2, 0)) {
                                m_ipcReduceMap[index]++;
                                // app_error("%s %d\n", "the IPC will pop frameMeta
                                // ", index);
                                fmeta2->reduceUseCount();
                            } else {
                                // app_error("%s %d\n", "the IPC cannot reduce
                                // second frameMeta : ", index);
                            }
                        }
                    }
                }
            }

            int frameSize = batchMeta->getFrameMetaSize();
            if (frameSize == 0) {
                bmetaPool->deallocate(batchMeta);

                continue;
            }

            batchMeta->creationTime = esclock() / 1000 - m_startTime;
            batchMeta->batchIndex = m_batchIndex;
            m_batchIndex++;
            CBaseMeta *temp = (CBaseMeta *)(batchMeta);
            app_debug("the batchMeta batchIndex %d, frameSize %d \n", batchMeta->batchIndex, frameSize);

            if ((iEofSize == iPreElementSize)) {
                temp->eosFlag = true;
            }

            app_debug("TransMitToNextToProcess batchIndex %d, frameSize %d \n", batchMeta->batchIndex, frameSize);

            TransMitToNextToProcess(temp);
            gBatchIndexCount++;
        }
    }
    app_debug("%s \n", " exit transToNext  ");
    return;
}