#ifndef _TESTSINK_ELEMENT_H__
#define _TESTSINK_ELEMENT_H__

#include "batch_meta.h"
#include "element.h"
#include "es_sys.h"
#include "es_sys_memory.h"
#include "es_vb_memory.h"
#include "object_meta.h"

class TestSinkElement : public CElement {
   public:
    TestSinkElement(const char *name = "testsink", BASE_ELEMENT_TYPE elementType = VIDEO_OUTPUT)
        : CElement(name, "", elementType){};
    ~TestSinkElement() = default;

    app_ret Init() override;
    app_ret Start() override;
    app_ret Wait() override;
    app_ret ProcessData(CBaseMeta *baseMeta, CElement const *previou = 0) override;
    app_ret ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *previou = 0) override;

   private:
    int mFrameCnt;
};
#endif  //_TESTSINK_ELEMENT_H__
