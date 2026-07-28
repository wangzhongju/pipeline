#ifndef _CPU_OP_H__
#define _CPU_OP_H__

#include "element.h"
#include "es_sys_memory.h"
#include "es_vb_memory.h"
#include "infer.h"
#include "postParams.h"

class CpuOp {
   public:
    CpuOp() = default;
    ~CpuOp() = default;

   public:
    app_ret classifyArgmax(CInferOutputMeta *inferOutput, std::vector<int> &maxIndexVec,
                           std::vector<float> &maxConfidenceVec);
    app_ret detectionPostprocess(CInferOutputMeta *inferOutput, std::vector<std::vector<DetectionOutput>> &outputVec);
};

#endif  //_CPU_OP_H__