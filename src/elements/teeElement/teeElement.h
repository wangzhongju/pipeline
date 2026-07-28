#ifndef _TEE_ELEMENT_H__
#define _TEE_ELEMENT_H__

#include "base_meta.h"
#include "element.h"

class TeeElement : public CElement {
   public:
    TeeElement(const char *name = "", BASE_ELEMENT_TYPE elementType = VIDEO_DECODER)
        : CElement(name, "", elementType){};
    ~TeeElement() = default;
    app_ret Init() override;
    app_ret ProcessData(CBaseMeta *baseMeta, CElement const *previousElement = 0) override;
};
#endif  //_TEE_ELEMENT_H__
