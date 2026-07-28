#define PL_LOG_ID PL_LOG_SDEMUX
#include "demuxElement.h"

app_ret DemuxElement::TransMitToNextToProcess(CBaseMeta *baseMeta, CElement *nextElement) {
    CFrameMeta *frameMeta = (CFrameMeta *)(baseMeta);
    app_ret ret = APP_SUCCESS;

    if (NULL != nextElement) {
        ret = nextElement->ProcessAndTransmit(frameMeta, this);
        if (APP_SUCCESS != ret) {
            return ret;
        }
    }
    return ret;
};

app_ret DemuxElement::ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) {
    CBatchMeta *batchMeta = (CBatchMeta *)(baseMeta);
    app_ret ret = APP_SUCCESS;
    bool eosflag = batchMeta->eosFlag;
    int frameMetaIndex = 0;
    app_debug("%s in\n", mName.c_str());

    if (eosflag && batchMeta->getFrameMetaSize() == 0) {
        for (int i = 0; i < m_NextElementVec.size(); i++) {
            CFrameMeta *frameMeta = new CFrameMeta;
            frameMeta->mMetaType = FRAME_META;
            frameMeta->eosFlag = eosflag;
            ret = TransMitToNextToProcess(frameMeta, m_NextElementVec[i]);
            if (APP_SUCCESS != ret) {
                batchMeta->reduceUseCount();
                return ret;
            }
        }
    }
    app_debug("framemetanum: %d, elementnum: %d \n", (int)(batchMeta->getFrameMetaSize()),
              (int)(m_NextElementVec.size()));
    for (frameMetaIndex = 0; frameMetaIndex < batchMeta->getFrameMetaSize() && frameMetaIndex < m_NextElementVec.size();
         frameMetaIndex++) {
        CFrameMeta *frameMeta = batchMeta->getFrameMeta(frameMetaIndex, true);
        int padIndex = frameMeta->padIndex;
        if (padIndex >= m_NextElementVec.size()) {
            frameMeta->reduceUseCount();
            app_debug("frameMetaIndex will release : %d \n", frameMetaIndex);
        } else {
            CElement *nextElement = m_NextElementVec[padIndex];
            frameMeta->eosFlag = eosflag;
            app_debug("padIndex: %d \n", padIndex);
            ret = TransMitToNextToProcess(frameMeta, nextElement);
            if (APP_SUCCESS != ret) {
                return ret;
            }
        }
    }

    // batchMeta->mImgMetas.clear();
    batchMeta->reduceUseCount();
    // delete batchMeta;//这里需要删除batchMeta,因为不会往后面传递；
    app_debug("%s out\n", mName.c_str());
    return APP_SUCCESS;
}

app_ret DemuxElement::ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *previousElement) {
    ProcessData((CBatchMeta *)baseMeta, this);
    return APP_SUCCESS;
}

app_ret DemuxElement::InfoQuery(void *data, BASE_QUERY_TYPE type, BASE_QUERY_DIRECTION direction, int padIndex,
                                CElement *inquirerElement) {
    app_ret ret = APP_FAILURE;
    if (direction == PREV_ELEMENT) {
        int num = m_PreviousElementVec.size();
        for (int i = 0; i < num; i++) {
            if (inquirerElement == m_PreviousElementVec[i]) {
                ret = m_PreviousElementVec[0]->InfoQuery(data, type, direction, i, this);
                break;
            }
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
}

extern "C" CElement *createEsDemuxElement(const char *name) { return new DemuxElement(name); }
