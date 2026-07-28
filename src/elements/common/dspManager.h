#ifndef _ESSDKPL_DSP_MANAGER_H__
#define _ESSDKPL_DSP_MANAGER_H__
#include <map>

#include "infer.h"
class EsPlDspManager {
   public:
    ES_S32 openDsp(const int dspID);
    int closeDsp(const int dspID);

   private:
    static std::map<uint, ES_S32> m_openDspFdMap;
    static std::map<uint, uint> m_openDspCountMap;
};
#endif  //_ESSDKPL_DSP_MANAGER_H__