#ifndef _QUEUE_ELEMENT_H__
#define _QUEUE_ELEMENT_H__

#include <thread>

#include "element.h"
#include "queue.h"

// 1：丢弃模式，优先丢弃旧的数据；
// 0: 普通的阻塞模式；
//-1： 无限长度队列模式； 最大为65535个长度；

class QueueElement : public CElement {
   public:
    QueueElement(int depth = 100, int type = 0, const char *name = "", const char *configFile = "", int dieIndex = 0)
        : CElement(name, configFile, dieIndex), queueType(type), m_queueData(type == -1 ? 65535 : depth, name) {
        mPerfType = ASYNC_PERF_ELEMENT;
    };
    ~QueueElement() = default;

    app_ret Init() override;
    app_ret Start() override;
    app_ret Wait() override;
    app_ret Finish() override;
    app_ret ProcessData(CBaseMeta *baseMeta, CElement const *previou = 0) override;
    app_ret ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *previou = 0) override;

   private:
    void threadFunc();

   private:
    BaseMetaQueue m_queueData;
    std::thread m_queueThread;
    int queueType;
    int64_t m_theRealDepth = 0;
};
#endif  //_QUEUE_ELEMENT_H__
