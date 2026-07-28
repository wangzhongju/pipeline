#ifndef _DEMUX_ELEMENT_H__
#define _DEMUX_ELEMENT_H__

#include "batch_meta.h"
#include "element.h"

class DemuxElement : public CElement {
   public:
    DemuxElement(const char *name = "") : CElement(name, ""){};
    ~DemuxElement() = default;

    app_ret TransMitToNextToProcess(CBaseMeta *baseMeta, CElement *nextElement);
    app_ret ProcessData(CBaseMeta *baseMeta, CElement const *previousElement = 0) override;
    app_ret ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *previousElement = 0) override;
    app_ret InfoQuery(void *data, BASE_QUERY_TYPE type, BASE_QUERY_DIRECTION direction, int padIndex,
                      CElement *inquirerElement) override;
};
#endif  //_DEMUX_ELEMENT_H__
