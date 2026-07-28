#ifndef _TESTSRC_ELEMENT_H__
#define _TESTSRC_ELEMENT_H__

#include <iostream>
#include <thread>
#include <memory>

#include "element.h"
#include "IDataGenerator.h"

class TestSrcElement : public CElement {
   public:
    TestSrcElement(const char *name = "testsrc", const char *config = "", int fps = 30, uint16_t loopNum = 1000)
        : CElement(name, config), mFps(fps), mLoopNum(loopNum), mFrameIndex(0){};
    ~TestSrcElement();

    app_ret Init() override;
    app_ret Start() override;
    app_ret Wait() override;
    app_ret ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) override;
    app_ret ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *previou = 0) override;

   private:
    app_ret threadFunc();

   private:
    std::thread m_testsrcThread;
    int mFps;
    uint16_t mLoopNum;
    uint16_t mFrameIndex;

    // The big list of booleans and data info structs is replaced by this single pointer.
    std::unique_ptr<IDataGenerator> m_dataGenerator;
};
#endif  //_TESTSRC_ELEMENT_H__