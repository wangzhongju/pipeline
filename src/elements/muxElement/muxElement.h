#ifndef _MUX_ELEMENT_H__
#define _MUX_ELEMENT_H__

#include <chrono>
#include <map>
#include <mutex>

#include "base_meta.h"
#include "batch_meta.h"
#include "element.h"
#include "notify.h"
template <class T>
class muxBlockQueue {
   public:
    explicit muxBlockQueue(int maxDepth = 1) : capacity(maxDepth){};

    ~muxBlockQueue();
    void push_back(const T& item);
    bool empty();
    bool full();
    bool pop(T& item);
    bool pop(T& item, int timeout);
    uint64_t size();

   private:
    std::deque<T> deq;
    size_t capacity;
    std::mutex mtx;
    std::condition_variable Consumer;
    std::condition_variable Producer;
};

class MuxElement : public CElement {
   public:
    MuxElement(uint64_t waitTime = 10, const char* elementName = "", int muxPoolSize = 6, int dieIndex = 0,
               const char* configFile = "")
        : CElement(elementName, configFile, dieIndex),
          muxPSize(muxPoolSize),
          m_batchIndex(0),
          m_waitTimeMs(waitTime),
          m_startTime(0),
          bmetaPool(NULL){};
    ~MuxElement(){};
    app_ret Init() override;
    app_ret Start() override;
    app_ret Finish() override;
    app_ret perfStat() override;
    app_ret Wait() override;
    app_ret ProcessData(CBaseMeta* baseMeta, CElement const* previousElement = 0) override;
    app_ret ProcessAndTransmit(CBaseMeta* baseMeta, CElement const* previousElement = 0) override;

   private:
    void transToNext();
    ulong m_startTime;
    muxBlockQueue<CFrameMeta*>* m_elementMapFrameMeta[MAX_VIDEO_GRP_NUM];
    std::map<const CElement*, int> m_elementIndexMap;
    std::thread m_transmitThread;
    uint64_t m_waitTimeMs;

    bool m_elementEosFlag[MAX_VIDEO_GRP_NUM];
    uint64_t m_batchIndex;
    std::vector<int> m_frameMetaIndexVec;
    std::vector<int> m_ipcIndexVec;
    std::map<int, int> m_ipcPopMap;
    std::map<int, int> m_ipcReduceMap;
    MetaPool<CBatchMeta>* bmetaPool;
    int muxPSize;
    ConditionNotifier notify;
};

// class MuxElement : public CElement
// {
// public:
//     MuxElement(uint64_t waitTime = 100, std::string elementName="",
//     std::string configFile=""):CElement(elementName, configFile),
//     m_waitTimeMs(waitTime){}; ~MuxElement(){};

//     app_ret Init() override;
//     app_ret ProcessData(CBaseMeta *baseMeta, CElement const*
//     previousElement=0); app_ret ProcessAndTransmit(CBaseMeta *baseMeta,
//     CElement const *previousElement = 0);

// private:
//     std::map<CElement const*, int> m_elementIndex;
//     std::map<CElement const*, bool> m_elementEosFlag;
//     std::map<CElement const*, CFrameMeta *> m_FrameMetaMap;
//     //线程安全要考虑； std::mutex mtx; std::chrono::steady_clock::time_point
//     batchMetaSendTime; uint64_t m_waitTimeMs;
// };
#endif  //_MUX_ELEMENT_H__
