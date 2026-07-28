#define PL_LOG_ID PL_LOG_TEE
#include "teeElement.h"

#include <shared_mutex>

app_ret TeeElement::Init() {
    if (m_PreviousElementVec.size() != 1 || m_NextElementVec.size() < 1) {
        return APP_FAILURE;
    }
    return APP_SUCCESS;
}

app_ret TeeElement::ProcessData(CBaseMeta* baseMeta, CElement const* previousElement) {
    std::unique_lock<std::shared_mutex> ulock(baseMeta->sLock);
    int nowUseCount = baseMeta->getUseCount();
    baseMeta->setUseCount(m_NextElementVec.size() + nowUseCount - 1);
    app_debug(" %s %s %s %d\n", "the name is ", mName.c_str(), "the baseMeta useCount is : ", baseMeta->getUseCount());
    ulock.unlock();
    return APP_SUCCESS;
}

extern "C" CElement* createEsTeeElement(const char* name, BASE_ELEMENT_TYPE elementType) {
    return new TeeElement(name, elementType);
}
