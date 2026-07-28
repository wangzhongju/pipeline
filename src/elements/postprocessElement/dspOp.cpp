#define PL_LOG_ID PL_LOG_POSTPROC
#include "dspOp.h"

#include <es_ak_api.h>
#include <es_ak_error.h>
#include <es_ak_types.h>

#define SIZE_4K 0x1000
#define SIZE_64K 0x10000
#define SIZE_512K 0x80000
#define SIZE_1M 0x100000
#define SIZE_2M 0x200000
#define ALIGN(size, alignment) (((size) + (alignment - 1)) & ~(alignment - 1))

ES_U64 align_size(ES_U64 size) {
    if (size <= SIZE_4K) {
        return ALIGN(size, SIZE_2M);
    } else if (size <= SIZE_64K) {
        return ALIGN(size, SIZE_2M);
    } else if (size <= SIZE_512K) {
        return ALIGN(size, SIZE_2M);
    } else if (size <= SIZE_1M) {
        return ALIGN(size, SIZE_2M);
    } else {
        return ALIGN(size, SIZE_2M);
    }
}

ES_DATA_PRECISION_E essdkplDataType2DspDataType(CDataType essdkplDataType) {
    switch (essdkplDataType) {
        case DATA_U8:
            return ES_PRECISION_UINT8;
            break;
        case DATA_U16:
            return ES_PRECISION_UINT16;
            break;
        case DATA_U32:
            return ES_PRECISION_UINT32;
            break;
        case DATA_U64:
            return ES_PRECISION_UINT64;
            break;
        case DATA_S8:
            return ES_PRECISION_INT8;
            break;
        case DATA_S16:
            return ES_PRECISION_INT16;
            break;
        case DATA_S32:
            return ES_PRECISION_INT32;
            break;
        case DATA_S64:
            return ES_PRECISION_INT64;
            break;
        case DATA_F16:
            return ES_PRECISION_FP16;
            break;
        case DATA_F32:
            return ES_PRECISION_FP32;
            break;
    }
    return ES_PRECISION_UNKNOWN;
}

CDataType dspDataType2essdkplDataType(ES_DATA_PRECISION_E dspDataType) {
    switch (dspDataType) {
        case ES_PRECISION_UINT8:
            return DATA_U8;
            break;
        case ES_PRECISION_UINT16:
            return DATA_U16;
            break;
        case ES_PRECISION_UINT32:
            return DATA_U32;
            break;
        case ES_PRECISION_UINT64:
            return DATA_U64;
            break;
        case ES_PRECISION_INT8:
            return DATA_S8;
            break;
        case ES_PRECISION_INT16:
            return DATA_S16;
            break;
        case ES_PRECISION_INT32:
            return DATA_S32;
            break;
        case ES_PRECISION_INT64:
            return DATA_S64;
            break;
        case ES_PRECISION_FP16:
            return DATA_F16;
            break;
        case ES_PRECISION_FP32:
            return DATA_F32;
            break;
    }
    return DATA_U8;
}

int ES_DSP_GetTypeSize(ES_DATA_PRECISION_E type) {
    switch (type) {
        case ES_PRECISION_UNKNOWN:
            return 0;
            break;
        case ES_PRECISION_INT8:
        case ES_PRECISION_UINT8:
            return 1;
            break;
        case ES_PRECISION_INT16:
        case ES_PRECISION_UINT16:
            return 2;
            break;
        case ES_PRECISION_INT32:
        case ES_PRECISION_UINT32:
            return 4;
            break;
        case ES_PRECISION_INT64:
        case ES_PRECISION_UINT64:
            return 8;
            break;
        case ES_PRECISION_FP16:
            return 2;
            break;
        case ES_PRECISION_FP32:
            return 4;
            break;
    }
    return 0;
}

DspOp::DspOp(PerformanceStatic **softmax, PerformanceStatic **argmax, PerformanceStatic **detection, int dspID,
             int dieIndex)
    : m_dspID(dspID), m_DieIndex(dieIndex) {
    m_softmaxCreateFlag = false;
    m_detectionCreateFlag = false;
    m_argmaxCreateFlag = false;
    softmaxPerformance = softmax;
    argmaxPerformance = argmax;
    detectionPerformance = detection;
    m_VBName = m_DieIndex == 0 ? "mmz_nid_0_part_0" : "mmz_nid_1_part_0";
    mDevice = m_DieIndex == 0 ? ES_AK_DEV_DIE0_CPU : ES_AK_DEV_DIE1_CPU;
    int ret = m_dspManager.openDsp(m_dspID);
    if (0 == ret) {
        const int devNum = 1;
        ES_AK_DEVICE_E devices[devNum] = {(AK_DEVICE_E)m_dspID};
        ret = ES_AK_SetDevice(devices, devNum);
        if (ret != APP_SUCCESS) {
            app_error("set dsp device failed.\n");
            return;
        }
    }
}

DspOp::~DspOp() {}

void DspOp::closeDspDevice() {
    app_debug(" %s %d\n", "will close dsp dev  : ", (int)m_dspID);
    m_dspManager.closeDsp(m_dspID);
    if (m_softmaxCreateFlag) {
        app_debug(" %s \n", " will destory softmax pool ");
        PL_ES_VB_DestroyPool(m_softmaxOutputPool);
        app_debug(" %s \n", " destory softmax pool end ");
    }

    if (m_argmaxCreateFlag) {
        app_debug(" %s \n", " will destory argmax pool ");
        PL_ES_VB_DestroyPool(m_argmaxOutputPool);
        PL_ES_VB_DestroyPool(m_argmaxIndexPool);
        app_debug(" %s \n", " destory argmax pool end ");
    }

    if (m_detectionCreateFlag) {
        app_debug(" %s \n", " will destory detection pool ");
        PL_ES_VB_DestroyPool(m_detectionOutputPool);
        PL_ES_VB_DestroyPool(m_detectionCountPool);
        app_debug(" %s \n", " destory detection pool end ");
    }

    return;
}

void shapeCopy(ES_TENSOR_S &left, ES_TENSOR_S &right) {
    left.shape[0] = right.shape[0];
    left.shape[1] = right.shape[1];
    left.shape[2] = right.shape[2];
    left.shape[3] = right.shape[3];
    left.shape[4] = right.shape[4];
    left.shape[5] = right.shape[5];
    return;
}

app_ret DspOp::classifyPostprocess(CInferOutputMeta *inferOutput, float softmaxScale, int argmaxK,
                                   std::vector<int> &maxIndexVec, std::vector<float> &maxConfidenceVec) {
    int ret = 0;
    if (inferOutput->onputDataInfo.size() != 1) {
        app_error(" %s \n", "the classification output size is not 1 ");
    }
    app_debug(" %s [ %d, %d, %d, %d ]\n", "the input dims [ n, h, w, c ] : ", inferOutput->onputDataInfo[0].dims.n,
              inferOutput->onputDataInfo[0].dims.h, inferOutput->onputDataInfo[0].dims.w,
              inferOutput->onputDataInfo[0].dims.c);

    ES_TENSOR_S softmaxInput;
    softmaxInput.shapeDim = 6;
    softmaxInput.shape[0] = inferOutput->onputDataInfo[0].dims.n;
    softmaxInput.shape[1] = inferOutput->onputDataInfo[0].dims.c;
    softmaxInput.shape[2] = inferOutput->onputDataInfo[0].dims.h;
    softmaxInput.shape[3] = inferOutput->onputDataInfo[0].dims.w;
    softmaxInput.shape[4] = 1;
    softmaxInput.shape[5] = softmaxInput.shape[4] * softmaxInput.shape[1];
    softmaxInput.dataType = essdkplDataType2DspDataType(inferOutput->onputDataInfo[0].dataType);
    softmaxInput.pData.memFd = inferOutput->memFd[0];
    softmaxInput.pData.offset = 0;
    softmaxInput.pData.size = inferOutput->onputDataInfo[0].dataSize;
    app_debug(" %s %d ; %s %d\n", "the softmaxInput data dsp type  is : ", (int)(softmaxInput.dataType),
              "the softmaxInput data essdkpl type  is : ", (int)(inferOutput->onputDataInfo[0].dataType));
    app_debug(" %s %lld , %s %d\n", "the softmaxInput data fd is : ", softmaxInput.pData.memFd,
              "the softmaxInput data size is : ", (int)(softmaxInput.pData.size));

    ES_TENSOR_S softmaxOutput;
    shapeCopy(softmaxOutput, softmaxInput);
    softmaxOutput.dataType = ES_PRECISION_FP32;
    int softmaxOutput_size = softmaxOutput.shape[0] * softmaxOutput.shape[1] * softmaxOutput.shape[2] *
                             softmaxOutput.shape[3] * softmaxOutput.shape[4] *
                             ES_DSP_GetTypeSize(softmaxOutput.dataType);

    app_debug(" %s [ %d, %d, %d, %d %d %d] %s %d\n",
              "the output dims [ n, c, h, w, C0, Cs ] : ", softmaxOutput.shape[0], softmaxOutput.shape[1],
              softmaxOutput.shape[2], softmaxOutput.shape[3], softmaxOutput.shape[4], softmaxOutput.shape[5],
              "the softmaxOutput softmaxOutput_size size is : ", (int)(softmaxOutput_size));

    if (!m_softmaxCreateFlag) {
        VB_POOL_CONFIG_S poolCfg = {0};
        poolCfg.blkCnt = 50;
        poolCfg.blkSize = softmaxOutput_size;
        poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
        memcpy(poolCfg.mmzName, m_VBName.c_str(), strlen(m_VBName.c_str()));
        app_debug(" %s \n", "will create m_softmaxOutputPool");
        ret = PL_ES_VB_CreatePool(&poolCfg, &m_softmaxOutputPool);
        if (ES_SUCCESS != ret) {
            app_error("%s create pool failed.", __FUNCTION__);
            return APP_FAILURE;
        }
        app_debug(" %s \n", "m_softmaxOutputPool create success");
        m_softmaxCreateFlag = true;
    }

    ES_U64 softmaxOutputFd = 0;
    ret = PL_ES_VB_GetBlock(m_softmaxOutputPool, softmaxOutput_size, m_VBName.c_str(), &softmaxOutputFd, "softmax");
    softmaxOutput.pData.memFd = softmaxOutputFd;
    softmaxOutput.pData.offset = 0;
    softmaxOutput.pData.size = softmaxOutput_size;
    app_debug(" %s %lld\n", " ES_DSP_SOFTMAX output fd is : ", softmaxOutput.pData.memFd);

    (*softmaxPerformance)->performanceStaticStart();
    app_debug(" %s \n", " ES_DSP_SOFTMAX start");
    ret = ES_AK_DSP_Softmax((AK_DEVICE_E)m_dspID, &softmaxInput, &softmaxOutput, softmaxScale);
    (*softmaxPerformance)->performanceStaticEnd();

    if (0 != ret) {
        app_error(" %s %d\n", " ES_DSP_SOFTMAX error, the ret is ", ret);
    }

    /********************************************************************/
    /*****************************argmax*********************************/
    /********************************************************************/
    app_debug(" %s \n",
              "************************************argmax**********************"
              "****************");
    ES_TENSOR_S argmaxInput = softmaxOutput;
    shapeCopy(argmaxInput, softmaxOutput);
    argmaxInput.pData.offset = 0;

    ES_TENSOR_S argmaxOutput;
    shapeCopy(argmaxOutput, argmaxInput);
    argmaxOutput.shape[1] = argmaxK;
    int argmaxOutputSize = argmaxOutput.shape[0] * argmaxOutput.shape[1] * argmaxOutput.shape[2] *
                           argmaxOutput.shape[3] * argmaxOutput.shape[4] * ES_DSP_GetTypeSize(argmaxOutput.dataType);
    app_debug(" %s [ %d, %d, %d, %d, %d ]\n  %s [ %d, %d, %d, %d, %d ]\n %s %d",
              "the argmax input dims [ n, c, h, w , C0] : ", argmaxInput.shape[0], argmaxInput.shape[1],
              argmaxInput.shape[2], argmaxInput.shape[3], argmaxInput.shape[4],
              "the argmax output dims [ n, c, h, w, C0 ] : ", argmaxOutput.shape[0], argmaxOutput.shape[1],
              argmaxOutput.shape[2], argmaxOutput.shape[3], argmaxOutput.shape[4],
              "the argmax intput/output datatype is : ", int(argmaxOutput.dataType));

    ES_TENSOR_S argmaxOutputIndex;
    argmaxOutputIndex = argmaxOutput;
    shapeCopy(argmaxOutputIndex, argmaxOutput);
    argmaxOutput.shape[1] = 1;
    argmaxOutputIndex.dataType = ES_PRECISION_UINT16;
    int argmaxOutputIndexSize = argmaxOutputIndex.shape[0] * argmaxOutputIndex.shape[1] * argmaxOutputIndex.shape[2] *
                                argmaxOutputIndex.shape[3] * argmaxOutputIndex.shape[4] *
                                ES_DSP_GetTypeSize(argmaxOutputIndex.dataType);

    app_debug(" %s [ %d, %d, %d, %d, %d ]\n %s [ %d ]\n",
              "the argmax output index dims [ n, c, h, w, C0 ] : ", argmaxOutputIndex.shape[0],
              argmaxOutputIndex.shape[1], argmaxOutputIndex.shape[2], argmaxOutputIndex.shape[3],
              argmaxOutputIndex.shape[4], "the argmax output index datatype is : ", int(argmaxOutputIndex.dataType));
    app_debug(" %s %d  %s %d \n", " the argmax output size is ", argmaxOutputSize, " the argmax output index size is ",
              argmaxOutputIndexSize);

    if (!m_argmaxCreateFlag) {
        VB_POOL_CONFIG_S argmxPoolCfg = {0};
        argmxPoolCfg.blkCnt = 50;
        argmxPoolCfg.blkSize = argmaxOutputSize;
        argmxPoolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
        memcpy(argmxPoolCfg.mmzName, m_VBName.c_str(), strlen(m_VBName.c_str()));
        app_debug(" %s \n", "will create m_argmaxOutputPool");
        ret = PL_ES_VB_CreatePool(&argmxPoolCfg, &m_argmaxOutputPool);
        if (ES_SUCCESS != ret) {
            app_error(" %s \n", " m_argmaxOutputPool create fail ");
            return APP_FAILURE;
        }
        app_debug(" %s \n", "m_argmaxOutputPool create success");

        VB_POOL_CONFIG_S argmxIndexPoolCfg = {0};
        argmxIndexPoolCfg.blkCnt = 50;
        argmxIndexPoolCfg.blkSize = argmaxOutputIndexSize;
        argmxIndexPoolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
        memcpy(argmxIndexPoolCfg.mmzName, m_VBName.c_str(), strlen(m_VBName.c_str()));
        app_debug(" %s \n", "will create m_argmaxIndexPool");
        ret = PL_ES_VB_CreatePool(&argmxIndexPoolCfg, &m_argmaxIndexPool);
        if (ES_SUCCESS != ret) {
            app_error(" %s \n", " m_argmaxIndexPool create fail ");
            return APP_FAILURE;
        }
        app_debug(" %s \n", "m_argmaxIndexPool create success");
        m_argmaxCreateFlag = true;
    }

    ES_U64 argmaxOutputFd = 0;
    app_debug(" %s \n", "argmax output PL_ES_VB_GetBlock");
    ret = PL_ES_VB_GetBlock(m_argmaxOutputPool, argmaxOutputSize, m_VBName.c_str(), &argmaxOutputFd, "argmaxOutput");
    app_debug(" %s %d\n", "PL_ES_VB_GetBlock ret is ", ret);

    argmaxOutput.pData.memFd = argmaxOutputFd;
    argmaxOutput.pData.size = argmaxOutputSize;
    argmaxOutput.pData.offset = 0;
    app_debug(" %s %lld %s %d %s %d\n", " argmaxOutput.pData.memFd is ", argmaxOutput.pData.memFd,
              " argmaxOutput.pData.size is : ", argmaxOutput.pData.size,
              "argmaxOutput.pData.offset is:", argmaxOutput.pData.offset);

    ES_U64 argmaxIndexFd = 0;
    app_debug(" %s \n", "argmax index PL_ES_VB_GetBlock");
    ret = PL_ES_VB_GetBlock(m_argmaxIndexPool, argmaxOutputIndexSize, m_VBName.c_str(), &argmaxIndexFd, "argmaxIndex");
    app_debug(" %s %d\n", "PL_ES_VB_GetBlock ret is ", ret);

    argmaxOutputIndex.pData.memFd = argmaxIndexFd;
    argmaxOutputIndex.pData.size = argmaxOutputIndexSize;
    argmaxOutputIndex.pData.offset = 0;
    app_debug(" %s %lld %s %d %s %d\n", " argmaxOutputIndex.pData.memFd is ", argmaxOutputIndex.pData.memFd,
              " argmaxOutputIndex.pData.size is : ", argmaxOutputIndex.pData.size,
              " argmaxOutputIndex.pData.offset is : ", argmaxOutputIndex.pData.offset);

    app_debug(" %s \n", " start ES_DSP_ARGMAX ");
    uint64_t argmaxStartTime = (uint64_t)esclock();

    (*argmaxPerformance)->performanceStaticStart();
    ret = ES_AK_DSP_Argmax((AK_DEVICE_E)m_dspID, &argmaxInput, &argmaxOutput, &argmaxOutputIndex, argmaxK, 1);
    (*argmaxPerformance)->performanceStaticEnd();
    uint64_t argmaxEndTime = (uint64_t)esclock();

    if (0 != ret) {
        app_error(" %s %d\n", " ES_DSP_ARGMAX error, the ret is ", ret);
    }
    app_debug(" %s %lu\n", " ES_DSP_ARGMAX cost time is ", argmaxEndTime - argmaxStartTime);
    app_debug(" %s \n", " end ES_DSP_ARGMAX ");

    {
        ES_VOID *pVirAddr = ES_SYS_Mmap(argmaxOutput.pData.memFd, argmaxOutputSize, SYS_CACHE_MODE_NOCACHE);
        float *tempOutput = (float *)pVirAddr;
        app_debug(" %s %.4f\n", " the argmax output data is ", tempOutput[0]);

        maxIndexVec.clear();
        maxConfidenceVec.clear();
        for (int tempOutputIndex = 0; tempOutputIndex < argmaxK * argmaxOutput.shape[0]; tempOutputIndex++) {
            maxConfidenceVec.push_back(tempOutput[tempOutputIndex]);
        }
        ES_S32 unmapRet = ES_SYS_Munmap(pVirAddr, argmaxOutputSize);
        ES_ASSERT(unmapRet == ES_SUCCESS, "post DSP classifyArgmax failed, the size is %d \n", argmaxOutputSize);

        pVirAddr = ES_SYS_Mmap(argmaxOutputIndex.pData.memFd, argmaxOutputIndexSize, SYS_CACHE_MODE_NOCACHE);
        uint16_t *tempIndex = (uint16_t *)pVirAddr;
        app_debug(" %s %d\n", " the argmax output index is ", tempIndex[0]);
        for (int tempOutputIndex = 0; tempOutputIndex < argmaxK * argmaxOutput.shape[0]; tempOutputIndex++) {
            maxIndexVec.push_back(tempIndex[tempOutputIndex]);
        }
        unmapRet = ES_SYS_Munmap(pVirAddr, argmaxOutputIndexSize);
        ES_ASSERT(unmapRet == ES_SUCCESS, "post DSP classifyArgmax failed, the size is %d \n", argmaxOutputIndexSize);
    }

    //*************************************************************************************//
    //*****************************realease
    // memory****************************************//
    //*************************************************************************************//
    app_debug(" %s \n", " PL_ES_VB_ReleaseBlock  softmaxOutputFd start ");
    app_debug(" %s %lld\n", " ES_DSP_SOFTMAX output fd is : ", softmaxOutputFd);
    PL_ES_VB_ReleaseBlock(softmaxOutputFd);
    app_debug(" %s \n", " PL_ES_VB_ReleaseBlock softmaxOutputFd end ");

    app_debug(" %s \n", " PL_ES_VB_ReleaseBlock  argmaxOutputFd start ");
    PL_ES_VB_ReleaseBlock(argmaxOutputFd);
    app_debug(" %s \n", " PL_ES_VB_ReleaseBlock argmaxOutputFd end ");

    app_debug(" %s \n", " PL_ES_VB_ReleaseBlock  argmaxIndexFd start ");
    PL_ES_VB_ReleaseBlock(argmaxIndexFd);
    app_debug(" %s \n", " PL_ES_VB_ReleaseBlock argmaxIndexFd end ");
    return APP_SUCCESS;
}

// app_ret DspOp::detectionPostprocess(CInferOutputMeta *inferOutput, DetectionOutParams &detectionParams,
// std::vector<DetectionOutput> &outputVec) {
//     int ret = 0;
//     std::vector<ES_TENSOR_S> detectionInputVec;
//     int inputSize = inferOutput->onputDataInfo.size();
//     if (inputSize == 0) {
//         app_error(" %s \n", " detection input fd size is 0 ");
//         return APP_FAILURE;
//     }
//     detectionInputVec.resize(inputSize);
//     for (int inputIndex = 0; inputIndex < inputSize; inputIndex++) {
//         detectionInputVec[inputIndex].shapeDim = 5;
//         detectionInputVec[inputIndex].shape[0] = inferOutput->onputDataInfo[inputIndex].dims.n;
//         detectionInputVec[inputIndex].shape[1] = inferOutput->onputDataInfo[inputIndex].dims.c;
//         detectionInputVec[inputIndex].shape[2] = inferOutput->onputDataInfo[inputIndex].dims.h;
//         detectionInputVec[inputIndex].shape[3] = inferOutput->onputDataInfo[inputIndex].dims.w;
//         detectionInputVec[inputIndex].shape[4] = 1;
//         detectionInputVec[inputIndex].dataType =
//         essdkplDataType2DspDataType(inferOutput->onputDataInfo[inputIndex].dataType);
//         detectionInputVec[inputIndex].pData.memFd = inferOutput->memFd[inputIndex];
//         detectionInputVec[inputIndex].pData.offset = 0;
//         detectionInputVec[inputIndex].pData.size = inferOutput->onputDataInfo[inputIndex].dataSize;
//         app_debug("%s %d ; %s [%d, %d, %d, %d, %d, %d, %lld, %d]\n", "the detectionInputVec data dsp type  is : ",
//         (int)(detectionInputVec[inputIndex].dataType), "the input info is : ", inputIndex,
//                   detectionInputVec[inputIndex].shape[0], detectionInputVec[inputIndex].shape[1],
//                   detectionInputVec[inputIndex].shape[2], detectionInputVec[inputIndex].shape[3],
//                   detectionInputVec[inputIndex].shape[4], detectionInputVec[inputIndex].pData.memFd,
//                   detectionInputVec[inputIndex].pData.size);
//     }

//     ES_TENSOR_S output;
//     output.shapeDim = 5;
//     output.shape[0] = 1;
//     output.shape[1] = 7;
//     output.shape[2] = 1;
//     output.shape[3] = detectionParams.maxBboxPerImg;
//     output.shape[4] = 1;
//     output.dataType = ES_PRECISION_FP32;
//     int outputSize = output.shape[0] * output.shape[1] * output.shape[2] * output.shape[3] * output.shape[4] *
//     ES_DSP_GetTypeSize(output.dataType); output.pData.offset = 0; output.pData.size = outputSize;

//     ES_TENSOR_S outputCount;
//     outputCount.shape[0] = detectionInputVec[0].shape[0];
//     outputCount.shape[1] = 1;
//     outputCount.shape[2] = 1;
//     outputCount.shape[3] = 1;
//     outputCount.shape[4] = 1;
//     outputCount.dataType = ES_PRECISION_INT32;
//     int outputCountSize = outputCount.shape[0] * outputCount.shape[1] * outputCount.shape[2] * outputCount.shape[3] *
//     outputCount.shape[4] * ES_DSP_GetTypeSize(outputCount.dataType); outputCount.pData.offset = 0;
//     outputCount.pData.size = outputCountSize;

//     if (!m_detectionCreateFlag) {
//         VB_POOL_CONFIG_S detectionOuputPoolCfg = {0};
//         detectionOuputPoolCfg.blkCnt = 50;
//         detectionOuputPoolCfg.blkSize = outputSize;
//         detectionOuputPoolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
//         memcpy(detectionOuputPoolCfg.mmzName, m_VBName.c_str(), strlen(m_VBName.c_str()));
//         app_debug(" %s \n", "will create m_detectionOutputPool");
//         ret = PL_ES_VB_CreatePool(&detectionOuputPoolCfg, &m_detectionOutputPool);
//         if (ES_SUCCESS != ret) {
//             app_error("%s create pool failed.", __FUNCTION__);
//             return APP_FAILURE;
//         }
//         app_debug(" %s \n", "m_detectionOutputPool create success");

//         VB_POOL_CONFIG_S detectionCountPoolCfg = {0};
//         detectionCountPoolCfg.blkCnt = 50;
//         detectionCountPoolCfg.blkSize = outputCountSize;
//         detectionCountPoolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
//         memcpy(detectionCountPoolCfg.mmzName, m_VBName.c_str(), strlen(m_VBName.c_str()));
//         app_debug(" %s \n", "will create m_detectionCountPool");
//         ret = PL_ES_VB_CreatePool(&detectionCountPoolCfg, &m_detectionCountPool);
//         if (ES_SUCCESS != ret) {
//             app_error("%s create pool failed.", __FUNCTION__);
//             return APP_FAILURE;
//         }
//         app_debug(" %s \n", "m_detectionCountPool create success");
//         m_detectionCreateFlag = true;
//     }

//     ES_U64 detectionCountFd = 0;
//     app_debug(" %s \n", "will PL_ES_VB_GetBlock");
//     ret = PL_ES_VB_GetBlock(m_detectionCountPool, outputCountSize, m_VBName.c_str(), &detectionCountFd,
//     "detectionOutCount"); app_debug(" %s %d\n", "PL_ES_VB_GetBlock ret is ", ret); outputCount.pData.memFd =
//     detectionCountFd;

//     ES_U64 detectionOutputFd = 0;
//     app_debug(" %s \n", "will PL_ES_VB_GetBlock");
//     ret = PL_ES_VB_GetBlock(m_detectionOutputPool, outputSize, m_VBName.c_str(), &detectionOutputFd,
//     "detectionOutputt"); app_debug(" %s %d\n", "PL_ES_VB_GetBlock ret is ", ret); output.pData.memFd =
//     detectionOutputFd; app_debug(" %s [%d, %d, %d, %d, %d, %ld, %d]\n", "the output info is : ", output.shape[0],
//     output.shape[1], output.shape[2], output.shape[3], output.shape[4], output.pData.memFd,
//               output.pData.size);

//     app_debug(" %s \n", " start ES_DSP_Detection_out ");
//     printf("detectionInputVec[0].pData.memFd: %lld;\n ", detectionInputVec[0].pData.memFd);
//     printf("detectionInputVec[0].pData.memFd: %lld;\n ", detectionInputVec[0].pData.memFd);

//     (*detectionPerformance)->performanceStaticStart();
//     // ret = ES_AK_DSP_DetectionOut((ES_TENSOR_S *)(&detectionInputVec[0]), inputSize, output, outputCount,
//     (ES_DET_NETWORK_E)detectionParams.detectionName, detectionParams.anchorNum,
//     //                              &detectionParams.anchorScale[0], detectionParams.inputWH.H,
//     detectionParams.inputWH.W, detectionParams.classNum, &detectionParams.inputScale[0],
//     //                              (ES_NMS_METHOD_E)detectionParams.nmsMethod,
//     (ES_IOU_METHOD_E)detectionParams.iouMethod, (ES_BOX_TYPE_E)1, (ES_BOOL)1, detectionParams.maxBboxPerClass,
//     //                              detectionParams.maxBboxPerImg, detectionParams.scoreThreshold,
//     detectionParams.iouThreshold, detectionParams.softnmssigma, detectionParams.imgOffset.offX,
//     //                              detectionParams.imgOffset.offY);
//     ret = ES_AK_DSP_DetectionOut(
//       detectionInputVec.data(), detectionInputVec.size(), output, outputCount,
//       (ES_DET_NETWORK_E)(detectionParams.detectionName), detectionParams.anchorNum,
//       detectionParams.anchorScale.data(), detectionParams.inputWH.H, detectionParams.inputWH.W,
//       detectionParams.classNum, detectionParams.inputScale.data(), (ES_NMS_METHOD_E)detectionParams.nmsMethod,
//       (ES_IOU_METHOD_E)detectionParams.iouMethod, (ES_BOX_TYPE_E)XminYminWH, (ES_BOOL)1,
//       detectionParams.maxBboxPerClass, detectionParams.maxBboxPerImg, detectionParams.scoreThreshold,
//       detectionParams.iouThreshold, detectionParams.softnmssigma, detectionParams.imgOffset.offX,
//       detectionParams.imgOffset.offY);
//     (*detectionPerformance)->performanceStaticEnd();

//     app_debug(" %s \n", " end ES_DSP_Detection_out ");
//     app_debug(" %s %d\n", " ES_DSP_Detection_out ret is ", ret);
//     if (0 != ret) {
//         app_error(" %s \n", " ES_DSP_Detection_out error !!! ");
//     }

//     {
//         std::vector<int32_t> perBatchBboxCount;
//         app_debug(" %s \n", "  detection outputCount map start ");
//         ES_VOID *pCountVirAdr = ES_SYS_Mmap(outputCount.pData.memFd, outputCount.pData.size, SYS_CACHE_MODE_NOCACHE);
//         app_debug(" %s %p\n", "  detection outputCount viraddr is : ", pCountVirAdr);
//         int32_t *pCount = (int32_t *)pCountVirAdr;
//         int iSumBbox = 0;
//         for (int icountDex = 0; icountDex < outputCount.shape[0]; icountDex++) {
//             perBatchBboxCount.push_back(pCount[icountDex]);
//             app_debug(" %s %d\n", " the bbox size is : ", pCount[icountDex]);
//             iSumBbox += pCount[icountDex];
//         }
//         app_debug(" %s %d\n", " the bbox sum size  is : ", iSumBbox);
//         app_debug(" %s \n", "detection outputCount unmap start");
//         ret = ES_SYS_Munmap(pCountVirAdr, outputCount.pData.size);
//         ES_ASSERT(ret == ES_SUCCESS, "post DSP DETECTION failed, the size is %d \n", outputCount.pData.size);
//         app_debug(" %s %d\n", " detection outputCount Munmap ret is ", ret);

//         app_debug(" %s \n", "  detection out map start ");
//         ES_VOID *pVirAddr = ES_SYS_Mmap(detectionOutputFd, outputSize, SYS_CACHE_MODE_NOCACHE);
//         app_debug(" %s %p\n", "  detection out viraddr is : ", pVirAddr);
//         float *tempOutput = (float *)pVirAddr;
//         for (int iOutputIndex = 0; iOutputIndex < output.shape[1] * output.shape[2]; iOutputIndex += output.shape[1])
//         {
//             DetectionOutput oneOutput;
//             oneOutput.batchID = tempOutput[iOutputIndex + 0];
//             if (abs(1 + oneOutput.batchID) < 0.0003) {
//                 app_debug(" %s \n", " get batchID -1 ");
//                 break;
//             }
//             oneOutput.classID = tempOutput[iOutputIndex + 1];
//             oneOutput.score = tempOutput[iOutputIndex + 2];
//             oneOutput.left = tempOutput[iOutputIndex + 3];
//             oneOutput.top = tempOutput[iOutputIndex + 4];
//             oneOutput.w = tempOutput[iOutputIndex + 5];
//             oneOutput.h = tempOutput[iOutputIndex + 6];
//             app_debug(" %s [%d, %d, %f, %f, %f, %f, %f]\n", "  detection out result is : ", oneOutput.batchID,
//             oneOutput.classID, oneOutput.score, oneOutput.left, oneOutput.top, oneOutput.w,
//                       oneOutput.h);
//             outputVec.push_back(oneOutput);

//             if (iSumBbox <= outputVec.size()) {
//                 app_debug(" %s \n", " the bbox count is equal sum ");
//                 break;
//             }
//         }

//         app_debug(" %s \n", "detection out unmap start");
//         ret = ES_SYS_Munmap(pVirAddr, outputSize);
//         ES_ASSERT(ret == ES_SUCCESS, "post DSP DETECTION failed, the size is %d \n", outputSize);
//         app_debug(" %s %d\n", " detection out Munmap ret is ", ret);

//         app_debug(" %s \n", " PL_ES_VB_ReleaseBlock  detectionOutputFd start ");
//         PL_ES_VB_ReleaseBlock(detectionOutputFd);
//         app_debug(" %s \n", " PL_ES_VB_ReleaseBlock detectionOutputFd end ");

//         app_debug(" %s \n", " PL_ES_VB_ReleaseBlock  detectionCountFd start ");
//         PL_ES_VB_ReleaseBlock(detectionCountFd);
//         app_debug(" %s \n", " PL_ES_VB_ReleaseBlock detectionCountFd end ");
//     }

//     return APP_SUCCESS;
// }

////获取不同数据类型的内存字节占用
int32_t convertPrecisionToBytes(ES_DATA_PRECISION_E precision) {
    switch (precision) {
        case ES_PRECISION_UNKNOWN:
            return 0;
            break;
        case ES_PRECISION_INT8:
        case ES_PRECISION_UINT8:
            return 1;
            break;
        case ES_PRECISION_INT16:
        case ES_PRECISION_UINT16:
            return 2;
            break;
        case ES_PRECISION_INT32:
        case ES_PRECISION_UINT32:
            return 4;
            break;
        case ES_PRECISION_INT64:
        case ES_PRECISION_UINT64:
            return 8;
            break;
        case ES_PRECISION_FP16:
            return 2;
            break;
        case ES_PRECISION_FP32:
            return 4;
            break;
    }
    return 0;
}

// 提取dsp输出的bbox的个数；
template <typename T>
std::vector<int32_t> extractBoxNum(void *data, int32_t count) {
    std::vector<int32_t> result;
    T *pData = (T *)data;

    for (int32_t i = 0; i < count; i++) {
        result.push_back(pData[i]);
    }

    return result;
}

// 提取 dsp 输出的bbox 的信息，包括了batchId, classId, confidence,  bbox
template <typename T>
std::vector<std::vector<std::vector<float>>> extractBoxInfo(void *data, std::vector<int32_t> &boxCnt, int32_t size) {
    std::vector<std::vector<std::vector<float>>> result;
    T *pData = (T *)data;

    for (auto &cnt : boxCnt) {
        std::vector<std::vector<float>> boxVec;
        for (int32_t i = 0; i < cnt; i++) {
            std::vector<float> boxInfo;
            for (int32_t j = 0; j < size; j++) {
                boxInfo.push_back(*pData);
                pData++;
            }
            boxVec.push_back(boxInfo);
        }
        result.push_back(boxVec);
    }

    return result;
}

// dsp 后处理配置参数
class DetectOutConfigs {
   public:
    // 网络模型分类；
    enum EsDetNetwork { ES_YOLOV3 = 1, ES_YOLOV4 = 2, ES_YOLOV5 = 3, ES_YOLOV7 = 4, ES_YOLOV8 = 5 };
    // nms 方法枚举；
    enum EsNmsMethod { ES_HARD_NMS = 0, ES_SOFT_NMS_GAUSSIAN, ES_SOFT_NMS_LINEAR };

    // iou 方法枚举；
    enum EsIouMethod { ES_IOU = 0, ES_GIOU, ES_DIOU };

    // 输出bbox类型的枚举
    enum EsBoxType {
        ES_XMIN_YMIN_XMAX_YMAX = 0,
        ES_XMIN_YMIN_W_H,
        ES_YMIN_XMIN_YMAX_XMAX,
        ES_XMID_YMID_W_H,
    };

    EsDetNetwork detectNet;                         // 网络模型
    int32_t outTensorNum;                           // 算子输出内存个数
    int32_t inTensorNum;                            // 算子输入内存个数
    std::vector<std::vector<int32_t>> inputShape;   // 内存输入的维度描述信息；
    std::vector<std::vector<int32_t>> outputShape;  // 内存输出的维度描述信息；
    std::vector<int32_t> inputDataType;             // 输入内存的数据类型；
    std::vector<int32_t> outputDataType;            // 输出内存的数据类型；
    int32_t anchorsNum;                             // 锚点数量；
    std::vector<float> anchorScale;  // 锚点scale信息，需要和inputShape 一一对应，需要具体确认
    int32_t imgH;                    // 模型的输入图像高度;
    int32_t imgW;                    // 模型的输入图像宽度；
    int32_t clsNum;                  // 模型的分类的数量；
    std::vector<float> inputScale;   // 输入内存的scale的尺寸；
    int32_t nmsMethod;               // nms
    int32_t iouMethod;               // iou
    int32_t outBoxType;
    int32_t coordNorm;
    int32_t maxBoxesPerClass;
    int32_t maxBoxesPerBatch;
    float scoreThreshold;
    float iouThreshold;
    float softNmsSigma;
    float effecImgOffsetX;
    float effecImgOffsetY;
};

// 定义 2MB 的字节数
#define TWO_MB (2 * 1024 * 1024)

// 宏定义实现向上取整功能
#define ROUND_UP_TO_TWO_MB(size) (((size) + TWO_MB - 1) / TWO_MB * TWO_MB)

app_ret DspOp::prapareDspOutFd(ES_TENSOR_S &output, ES_TENSOR_S &outputCount, DetectionOutParams &detectionParams) {
    int32_t ret = 0;

    memset(&output, 0x00, sizeof(output));
    output.shapeDim = 5;
    output.shape[0] = 1;
    output.shape[1] = 7;
    output.shape[2] = 1;
    output.shape[3] = detectionParams.maxBboxPerImg;
    output.shape[4] = 1;
    output.dataType = ES_PRECISION_FP32;
    int outputSize = output.shape[0] * output.shape[1] * output.shape[2] * output.shape[3] * output.shape[4] *
                     ES_DSP_GetTypeSize(output.dataType);
    output.pData.offset = 0;
    outputSize = align_size((outputSize));
    output.pData.size = outputSize;

    memset(&outputCount, 0x00, sizeof(outputCount));
    outputCount.shapeDim = 5;
    outputCount.shape[0] = 1;  // detectionInputVec[0].shape[0];
    outputCount.shape[1] = 1;
    outputCount.shape[2] = 1;
    outputCount.shape[3] = 1;
    outputCount.shape[4] = 1;
    outputCount.dataType = ES_PRECISION_INT32;
    int outputCountSize = outputCount.shape[0] * outputCount.shape[1] * outputCount.shape[2] * outputCount.shape[3] *
                          outputCount.shape[4] * ES_DSP_GetTypeSize(outputCount.dataType);
    outputCount.pData.offset = 0;
    outputCountSize = align_size((outputCountSize));
    outputCount.pData.size = outputCountSize;

    if (!m_detectionCreateFlag) {
        VB_POOL_CONFIG_S detectionOuputPoolCfg = {0};
        detectionOuputPoolCfg.blkCnt = 4;
        detectionOuputPoolCfg.blkSize = align_size((outputSize));  // outputSize;
        detectionOuputPoolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
        memcpy(detectionOuputPoolCfg.mmzName, m_VBName.c_str(), strlen(m_VBName.c_str()));
        app_debug(" %s \n", "will create m_detectionOutputPool");
        ret = PL_ES_VB_CreatePool(&detectionOuputPoolCfg, &m_detectionOutputPool);
        if (ES_SUCCESS != ret) {
            app_error("%s create pool failed.", __FUNCTION__);
            return APP_FAILURE;
        }
        app_debug(" %s \n", "m_detectionOutputPool create success");

        VB_POOL_CONFIG_S detectionCountPoolCfg = {0};
        detectionCountPoolCfg.blkCnt = 5;
        detectionCountPoolCfg.blkSize = align_size((outputCountSize));  // outputCountSize;
        detectionCountPoolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
        memcpy(detectionCountPoolCfg.mmzName, m_VBName.c_str(), strlen(m_VBName.c_str()));
        app_debug(" %s \n", "will create m_detectionCountPool");
        ret = PL_ES_VB_CreatePool(&detectionCountPoolCfg, &m_detectionCountPool);
        if (ES_SUCCESS != ret) {
            app_error("%s create pool failed.", __FUNCTION__);
            return APP_FAILURE;
        }
        app_debug(" %s \n", "m_detectionCountPool create success");
        m_detectionCreateFlag = true;
    }

    ES_U64 detectionCountFd = 0;
    app_debug(" %s \n", "will PL_ES_VB_GetBlock");
    ret = PL_ES_VB_GetBlock(m_detectionCountPool, outputCountSize, m_VBName.c_str(), &detectionCountFd,
                            "detectionOutCount");
    if (ES_SUCCESS != ret) {
        app_error("%s PL_ES_VB_GetBlock failed.", __FUNCTION__);
        return APP_FAILURE;
    }
    app_debug(" %s %d\n", "PL_ES_VB_GetBlock ret is ", ret);
    outputCount.pData.memFd = detectionCountFd;

    ES_U64 detectionOutputFd = 0;
    app_debug(" %s \n", "will PL_ES_VB_GetBlock");
    ret =
        PL_ES_VB_GetBlock(m_detectionOutputPool, outputSize, m_VBName.c_str(), &detectionOutputFd, "detectionOutputt");
    if (ES_SUCCESS != ret) {
        app_error("%s PL_ES_VB_GetBlock failed.", __FUNCTION__);
        return APP_FAILURE;
    }
    app_debug(" %s %d\n", "PL_ES_VB_GetBlock ret is ", ret);
    output.pData.memFd = detectionOutputFd;
    app_debug(" %s [%d, %d, %d, %d, %d, %ld, %d]\n", "the output info is : ", output.shape[0], output.shape[1],
              output.shape[2], output.shape[3], output.shape[4], output.pData.memFd, output.pData.size);

    app_debug(" %s \n", " start ES_DSP_Detection_out ");

    return ret;
}

app_ret DspOp::prepareDspCFG(ES_DETECTION_OUT_CFG &stDspCfg, DetectionOutParams &detectionParams) {
    int32_t ret = 0;

    stDspCfg.anchorsNum = detectionParams.anchorNum;
    memcpy(stDspCfg.anchorScale, detectionParams.anchorScale.data(),
           detectionParams.anchorScale.size() * sizeof(ES_FLOAT));
    stDspCfg.imgH = detectionParams.inputWH.H;
    stDspCfg.imgW = detectionParams.inputWH.W;
    stDspCfg.clsNum = detectionParams.classNum;
    memcpy(stDspCfg.inputScale, detectionParams.inputScale.data(),
           detectionParams.inputScale.size() * sizeof(ES_FLOAT));
    stDspCfg.nmsMethod = (ES_NMS_METHOD_E)detectionParams.nmsMethod;
    stDspCfg.iouMethod = (ES_IOU_METHOD_E)detectionParams.iouMethod;
    stDspCfg.outBoxType = (ES_BOX_TYPE_E)XminYminWH;

    stDspCfg.coordNorm = (ES_BOOL)1;

    stDspCfg.maxBoxesPerClass = detectionParams.maxBboxPerClass;
    stDspCfg.maxBoxesPerBatch = detectionParams.maxBboxPerImg;
    stDspCfg.scoreThreshold = detectionParams.scoreThreshold;
    stDspCfg.iouThreshold = detectionParams.iouThreshold;
    stDspCfg.softNmsSigma = detectionParams.softnmssigma;
    stDspCfg.effecImgOffsetX = detectionParams.imgOffset.offX;
    stDspCfg.effecImgOffsetY = detectionParams.imgOffset.offY;
    return ret;
}

template <typename T>
std::vector<int32_t> DspOp::extractBoxNum(void *data, int32_t count) {
    std::vector<int32_t> result;
    T *pData = (T *)data;

    for (int32_t i = 0; i < count; i++) {
        result.push_back(pData[i]);
    }

    return result;
}

template <typename T>
std::vector<std::vector<DetectionOutput>> DspOp::extractBoxInfo(void *data, std::vector<int32_t> &boxCnt,
                                                                int32_t size) {
    std::vector<std::vector<DetectionOutput>> result;
    T *pData = (T *)data;

    for (auto &cnt : boxCnt) {
        std::vector<DetectionOutput> boxVec;
        for (int32_t i = 0; i < cnt; i++) {
            DetectionOutput boxInfo;
            boxInfo.batchID = static_cast<int>(*pData++);
            boxInfo.classID = static_cast<int>(*pData++);
            boxInfo.score = *pData++;
            boxInfo.left = *pData++;
            boxInfo.top = *pData++;
            boxInfo.w = *pData++;
            boxInfo.h = *pData++;
            boxVec.push_back(boxInfo);
        }
        result.push_back(boxVec);
    }

    return result;
}
app_ret DspOp::extractDspOut(ES_TENSOR_S &output, ES_TENSOR_S &outputCount, std::vector<int32_t> &boxCount,
                             std::vector<std::vector<DetectionOutput>> &boxInfo) {
    int32_t ret = 0;

    app_debug(" %s \n", "  detection outputCount map start ");
    ES_VOID *poutputCount =
        ES_SYS_Mmap(outputCount.pData.memFd, align_size(outputCount.pData.size), SYS_CACHE_MODE_NOCACHE);
    app_debug(" %s %p\n", "  detection outputCount viraddr is : ", poutputCount);

    app_debug(" %s \n", "  detection out map start ");
    ES_VOID *poutputData = ES_SYS_Mmap(output.pData.memFd, align_size(output.pData.size), SYS_CACHE_MODE_NOCACHE);
    app_debug(" %s %p\n", "  detection out viraddr is : ", poutputData);

    if (outputCount.dataType == ES_PRECISION_INT32) {
        boxCount = extractBoxNum<int32_t>(poutputCount, (int32_t)outputCount.shape[0]);
    } else {
        app_error("post process box count not support datatype: %d\n", outputCount.dataType);
    }

    if (output.dataType == ES_PRECISION_FP32) {
        boxInfo = extractBoxInfo<float>(poutputData, boxCount, (int32_t)output.shape[1]);
    } else {
        app_error("post process box info not supported data type: %d\n", output.dataType);
    }

    ret = ES_SYS_Munmap(poutputCount, outputCount.pData.size);
    ES_ASSERT(ret == ES_SUCCESS, "post DSP DETECTION failed, the size is %d \n", outputCount.pData.size);
    app_debug(" %s %d\n", " detection outputCount Munmap ret is ", ret);

    ret = ES_SYS_Munmap(poutputData, output.pData.size);
    ES_ASSERT(ret == ES_SUCCESS, "post DSP DETECTION failed, the size is %d \n", output.pData.size);
    app_debug(" %s %d\n", " detection outputCount Munmap ret is ", ret);

    app_debug(" %s \n", " PL_ES_VB_ReleaseBlock  detectionOutcntFd start ");
    PL_ES_VB_ReleaseBlock(outputCount.pData.memFd);
    app_debug(" %s \n", " PL_ES_VB_ReleaseBlock detectionOutcntFd end ");

    app_debug(" %s \n", " PL_ES_VB_ReleaseBlock  detectionCountFd start ");
    PL_ES_VB_ReleaseBlock(output.pData.memFd);
    app_debug(" %s \n", " PL_ES_VB_ReleaseBlock detectionCountFd end ");

    return ret;
}
#if 0
// dsp 后处理；
int32_t DspOp::postprocess_detectionOut(std::vector<ES_TENSOR_S> &inTensors, DetectionOutParams &detectionParams, std::vector<DetectionOutput> &outputVec)
{
  std::vector<std::vector<std::vector<float>>> boxInfo;
  DetectOutConfigs mDetectOutConfigs;
  mDetectOutConfigs.detectNet = (DetectOutConfigs::EsDetNetwork)detectionParams.detectionName;//(DetectOutConfigs::EsDetNetwork)3;
  mDetectOutConfigs.inTensorNum = inTensors.size();
  mDetectOutConfigs.outTensorNum = 2;

  // 配置dsp输入的内存的维度信息等；
//   for(int index=0; index<mDetectOutConfigs.inTensorNum; index++)
//   {
//     inTensors[index].shapeDim = 5;
//     inTensors[index].shape[0] = m_inputDescription[index].dims.n;
//     inTensors[index].shape[1] = m_inputDescription[index].dims.c;
//     inTensors[index].shape[2] = m_inputDescription[index].dims.h;
//     inTensors[index].shape[3] = m_inputDescription[index].dims.w;
//     inTensors[index].shape[4] = 1;
//     inTensors[index].dataType = convertDataType(m_inputDescription[index].dataType);
//     inTensors[index].pData.memFd = inputFd[index];
//     inTensors[index].pData.offset = 0;
//     inTensors[index].pData.size = m_inputDescription[index].bufferSize;
//   }

  // 配置后处理算法的各种参数；
  mDetectOutConfigs.anchorsNum = detectionParams.anchorNum;
  std::vector<float>tempVec {10,13, 16,30, 33,23, 30,61, 62,45, 59,119, 116,90, 156,198, 373,326};
  for(int tempIndex=0; tempIndex<detectionParams.anchorScale.size(); tempIndex++)
  {
    mDetectOutConfigs.anchorScale.push_back(detectionParams.anchorScale[tempIndex]);
  }

  mDetectOutConfigs.imgH = detectionParams.inputWH.H;
  mDetectOutConfigs.imgW = detectionParams.inputWH.W;
  mDetectOutConfigs.clsNum = detectionParams.classNum;
  std::vector<float>tempVec2 {0.1632639467716217, 0.16429603099822998, 0.1704125851392746};
  for(int tempIndex=0; tempIndex<detectionParams.inputScale.size(); tempIndex++)
  {
    mDetectOutConfigs.inputScale.push_back(detectionParams.inputScale[tempIndex]);
  }
  mDetectOutConfigs.nmsMethod = (ES_NMS_METHOD_E)detectionParams.nmsMethod;
  mDetectOutConfigs.iouMethod = (ES_IOU_METHOD_E)detectionParams.iouMethod;
  mDetectOutConfigs.outBoxType = (ES_BOX_TYPE_E)XminYminWH;

  mDetectOutConfigs.coordNorm = (ES_BOOL)1;

  mDetectOutConfigs.maxBoxesPerClass = detectionParams.maxBboxPerClass;
  mDetectOutConfigs.maxBoxesPerBatch = detectionParams.maxBboxPerImg;
  mDetectOutConfigs.scoreThreshold = detectionParams.scoreThreshold;
  mDetectOutConfigs.iouThreshold = detectionParams.iouThreshold;
  mDetectOutConfigs.softNmsSigma = detectionParams.softnmssigma;
  mDetectOutConfigs.effecImgOffsetX = detectionParams.imgOffset.offX;
  mDetectOutConfigs.effecImgOffsetY = detectionParams.imgOffset.offY;

  // dsp的后处理输出两个内存，一个内存描述输出的bbox的数量，另外一个内存描述输出的检测到的物体的信息；
  ES_TENSOR_S output, outputCount;
  int32_t outputSize, outputCountSize;
  void *outputData = nullptr;
  void *outputCountData = nullptr;
  std::vector<int32_t> boxCount;
  int32_t ret;


  // 检测到的物体的描述信息配置；
  memset(&output, 0x00, sizeof(output));
  memset(&outputCount, 0x00, sizeof(outputCount));
  output.shapeDim = 5;
  output.shape[0] = 1;
  output.shape[1] = 7;  // b_id, clsid, score, x, y, x, y
  output.shape[2] = 1;
  output.shape[3] = detectionParams.maxBboxPerImg;
  output.shape[4] = 1;
  output.dataType = ES_PRECISION_FP32;
  outputSize = output.shape[0] * output.shape[1] * output.shape[2] * output.shape[3] * output.shape[4] *
                convertPrecisionToBytes(output.dataType);
  output.pData.offset = 0;
  output.pData.size = outputSize;

//   ret = prepareSysMem(output.pData.memFd, outputSize);
//   if (ret != ES_AK_SUCCESS) {
//       printf("prepareSysMem output failed\n");
//       return ret;
//   }

  // 检测到的物体的个数信息；
  outputCount.shapeDim = 5;
  outputCount.shape[0] = inTensors[0].shape[0];
  outputCount.shape[1] = 1;
  outputCount.shape[2] = 1;
  outputCount.shape[3] = 1;
  outputCount.shape[4] = 1;
  outputCount.dataType = ES_PRECISION_INT32;

  outputCountSize = outputCount.shape[0] * outputCount.shape[1] * outputCount.shape[2] * outputCount.shape[3] *
                    outputCount.shape[4] * convertPrecisionToBytes(outputCount.dataType);
  outputCount.pData.offset = 0;

  outputCount.pData.size = outputCountSize;


//   ret = prepareSysMem(outputCount.pData.memFd, outputCountSize);
//   if (ret != ES_AK_SUCCESS) {
//       printf("prepareSysMem outputCount failed, ret: 0x%x!\n", ret);
//       goto exited;
//   }

    static bool m_detectionCreateFlag = false;
    if (!m_detectionCreateFlag) {
        VB_POOL_CONFIG_S detectionOuputPoolCfg = {0};
        detectionOuputPoolCfg.blkCnt = 50;
        detectionOuputPoolCfg.blkSize = outputSize;
        detectionOuputPoolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
        memcpy(detectionOuputPoolCfg.mmzName, m_VBName.c_str(), strlen(m_VBName.c_str()));
        app_debug(" %s \n", "will create m_detectionOutputPool");
        ret = PL_ES_VB_CreatePool(&detectionOuputPoolCfg, &m_detectionOutputPool);
        if (ES_SUCCESS != ret) {
            app_error("%s create pool failed.", __FUNCTION__);
            return APP_FAILURE;
        }
        app_debug(" %s \n", "m_detectionOutputPool create success");

        VB_POOL_CONFIG_S detectionCountPoolCfg = {0};
        detectionCountPoolCfg.blkCnt = 50;
        detectionCountPoolCfg.blkSize = outputCountSize;
        detectionCountPoolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
        memcpy(detectionCountPoolCfg.mmzName, m_VBName.c_str(), strlen(m_VBName.c_str()));
        app_debug(" %s \n", "will create m_detectionCountPool");
        ret = PL_ES_VB_CreatePool(&detectionCountPoolCfg, &m_detectionCountPool);
        if (ES_SUCCESS != ret) {
            app_error("%s create pool failed.", __FUNCTION__);
            return APP_FAILURE;
        }
        app_debug(" %s \n", "m_detectionCountPool create success");
        m_detectionCreateFlag = true;
    }

    ES_U64 detectionCountFd = 0;
    app_debug(" %s \n", "will PL_ES_VB_GetBlock");
    ret = PL_ES_VB_GetBlock(m_detectionCountPool, outputCountSize, m_VBName.c_str(), &detectionCountFd, "detectionOutCount");
    app_debug(" %s %d\n", "PL_ES_VB_GetBlock ret is ", ret);
    outputCount.pData.memFd = detectionCountFd;

    ES_U64 detectionOutputFd = 0;
    app_debug(" %s \n", "will PL_ES_VB_GetBlock");
    ret = PL_ES_VB_GetBlock(m_detectionOutputPool, outputSize, m_VBName.c_str(), &detectionOutputFd, "detectionOutputt");
    app_debug(" %s %d\n", "PL_ES_VB_GetBlock ret is ", ret);
    output.pData.memFd = detectionOutputFd;
    app_debug(" %s [%d, %d, %d, %d, %d, %ld, %d]\n", "the output info is : ", output.shape[0], output.shape[1], output.shape[2], output.shape[3], output.shape[4], output.pData.memFd,
              output.pData.size);

    app_debug(" %s \n", " start ES_DSP_Detection_out ");

  (*detectionPerformance)->performanceStaticStart();
  ret = ES_AK_DSP_DetectionOut(
      inTensors.data(), inTensors.size(), output, outputCount, (ES_DET_NETWORK_E)mDetectOutConfigs.detectNet,
      mDetectOutConfigs.anchorsNum, mDetectOutConfigs.anchorScale.data(), mDetectOutConfigs.imgH,
      mDetectOutConfigs.imgW, mDetectOutConfigs.clsNum, mDetectOutConfigs.inputScale.data(),
      (ES_NMS_METHOD_E)mDetectOutConfigs.nmsMethod, (ES_IOU_METHOD_E)mDetectOutConfigs.iouMethod,
      (ES_BOX_TYPE_E)mDetectOutConfigs.outBoxType, (ES_BOOL)mDetectOutConfigs.coordNorm,
      mDetectOutConfigs.maxBoxesPerClass, mDetectOutConfigs.maxBoxesPerBatch, mDetectOutConfigs.scoreThreshold,
      mDetectOutConfigs.iouThreshold, mDetectOutConfigs.softNmsSigma, mDetectOutConfigs.effecImgOffsetX,
      mDetectOutConfigs.effecImgOffsetY);
  if (ret != ES_AK_SUCCESS) {
      printf("ES_AK_DSP_DetectionOut failed.\n");
  }
  (*detectionPerformance)->performanceStaticEnd();

  outputData = ES_SYS_Mmap(output.pData.memFd, output.pData.size, SYS_CACHE_MODE_NOCACHE);
  if (!outputData) {
      printf("output ES_SYS_Mmap failed\n");
  }

  outputCountData = ES_SYS_Mmap(outputCount.pData.memFd, outputCount.pData.size, SYS_CACHE_MODE_NOCACHE);
  if (!outputCountData) {
      printf("outputCount ES_SYS_Mmap failed\n");
  }


  if (outputCount.dataType == ES_PRECISION_INT32) {
      boxCount = extractBoxNum<int32_t>(outputCountData, (int32_t)outputCount.shape[0]);
  } else {
      printf("post process box count not support datatype: %d\n", outputCount.dataType);
  }

  int iSumBbox = 0;
        for (int icountDex = 0; icountDex < boxCount.size(); icountDex++) {
            iSumBbox += boxCount[icountDex];
        }

//   if (output.dataType == ES_PRECISION_FP32) {
//       boxInfo = extractBoxInfo<float>(outputData, boxCount, (int32_t)output.shape[1]);
//   } else {
//       printf("post process box info not supported data type: %d\n", output.dataType);
//   }

       float *tempOutput = (float *)outputData;
        for (int iOutputIndex = 0; iOutputIndex < output.shape[1] * output.shape[2]; iOutputIndex += output.shape[1]) {
            DetectionOutput oneOutput;
            oneOutput.batchID = tempOutput[iOutputIndex + 0];
            if (abs(1 + oneOutput.batchID) < 0.0003) {
                app_debug(" %s \n", " get batchID -1 ");
                break;
            }
            oneOutput.classID = tempOutput[iOutputIndex + 1];
            oneOutput.score = tempOutput[iOutputIndex + 2];
            oneOutput.left = tempOutput[iOutputIndex + 3];
            oneOutput.top = tempOutput[iOutputIndex + 4];
            oneOutput.w = tempOutput[iOutputIndex + 5];
            oneOutput.h = tempOutput[iOutputIndex + 6];
            app_debug(" %s [%d, %d, %f, %f, %f, %f, %f]\n", "  detection out result is : ", oneOutput.batchID, oneOutput.classID, oneOutput.score, oneOutput.left, oneOutput.top, oneOutput.w,
                      oneOutput.h);
            outputVec.push_back(oneOutput);

            if (iSumBbox <= outputVec.size()) {
                app_debug(" %s \n", " the bbox count is equal sum ");
                break;
            }
        }


    if (outputData) {
        ES_SYS_Munmap(outputData, output.pData.size);
    }
    if (outputCountData) {
        ES_SYS_Munmap(outputCountData, outputCount.pData.size);
    }

    app_debug(" %s \n", " PL_ES_VB_ReleaseBlock  detectionOutputFd start ");
    PL_ES_VB_ReleaseBlock(detectionOutputFd);
    app_debug(" %s \n", " PL_ES_VB_ReleaseBlock detectionOutputFd end ");

    app_debug(" %s \n", " PL_ES_VB_ReleaseBlock  detectionCountFd start ");
    PL_ES_VB_ReleaseBlock(detectionCountFd);
    app_debug(" %s \n", " PL_ES_VB_ReleaseBlock detectionCountFd end ");



  return 0;
}
#endif

int32_t rtmPose(std::vector<ES_TENSOR_S> inTensors, std::vector<ES_POSE_POINT> &poseResult,
                RtmposeParams &rtmposeParams) {
    int32_t extend_width = inTensors[0].shape[2];
    int32_t extend_height = inTensors[1].shape[2];
    int32_t numPoints = inTensors[0].shape[1];

    std::vector<int16_t> simccXResult;
    std::vector<int16_t> simccYResult;

    float scaleX = rtmposeParams.scale_x;
    float scaleY = rtmposeParams.scale_y;

    if (inTensors.size() != 2) {
        printf("select_max input tensor size is not 2\n");
        return -1;
    }

    uint64_t fd = inTensors[0].pData.memFd;
    uint64_t size = inTensors[0].pData.size;
    uint64_t offset = inTensors[0].pData.offset;
    int16_t *pOutData = (int16_t *)ES_SYS_Mmap(fd, size + offset, SYS_CACHE_MODE_NOCACHE);
    if (!pOutData) {
        printf("Mmap tensor[0] failed!\n");
        return ES_FAILURE;
    }

    simccXResult.assign(pOutData + offset, pOutData + offset + size / sizeof(int16_t));
    ES_SYS_Munmap(pOutData, size + offset);

    fd = inTensors[1].pData.memFd;
    size = inTensors[1].pData.size;
    offset = inTensors[1].pData.offset;
    pOutData = (int16_t *)ES_SYS_Mmap(fd, size + offset, SYS_CACHE_MODE_NOCACHE);
    if (!pOutData) {
        printf("Mmap tensor[1] failed!\n");
        return ES_FAILURE;
    }

    simccYResult.assign(pOutData + offset, pOutData + offset + size / sizeof(int16_t));
    ES_SYS_Munmap(pOutData, size + offset);

    std::vector<float> floatVecX(simccXResult.size());
    std::vector<float> floatVecY(simccYResult.size());

    std::transform(simccXResult.begin(), simccXResult.end(), floatVecX.begin(),
                   [scaleX](int16_t x) { return static_cast<float>(x) * scaleX; });

    std::transform(simccYResult.begin(), simccYResult.end(), floatVecY.begin(),
                   [scaleY](int16_t x) { return static_cast<float>(x) * scaleY; });

    for (uint32_t i = 0; i < numPoints; ++i) {
        // find the maximum and maximum indexes in the value of each Extend_width length
        auto xBiggestIter =
            std::max_element(floatVecX.begin() + i * extend_width, floatVecX.begin() + i * extend_width + extend_width);
        uint32_t poseX = (std::distance(floatVecX.begin() + i * extend_width, xBiggestIter)) / 2;
        float scoreX = *xBiggestIter;

        // find the maximum and maximum indexes in the value of each exten_height length
        auto yBiggestIter = std::max_element(floatVecY.begin() + i * extend_height,
                                             floatVecY.begin() + i * extend_height + extend_height);
        uint32_t poseY = (std::distance(floatVecY.begin() + i * extend_height, yBiggestIter)) / 2;
        float scoreY = *yBiggestIter;

        // get point confidence
        float score = std::max(scoreX, scoreY);
        ES_POSE_POINT tempPoint;
        tempPoint.x = poseX;
        tempPoint.y = poseY;
        tempPoint.score = score;
        poseResult.emplace_back(tempPoint);
    }

    return 0;
}

app_ret DspOp::rtmposePostprocess(CInferOutputMeta *inferOutput, RtmposeParams &rtmposeParams,
                                  std::vector<std::vector<RtmPosePointInfo>> &outputVec) {
    int ret = 0;
    std::vector<ES_TENSOR_S> detectionInputVec;
    int inputSize = inferOutput->onputDataInfo.size();
    if (inputSize == 0) {
        app_error(" %s \n", " detection input fd size is 0 ");
        return APP_FAILURE;
    }
    detectionInputVec.resize(inputSize);
    for (int inputIndex = 0; inputIndex < inputSize; inputIndex++) {
        detectionInputVec[inputIndex].shapeDim = 2;
        detectionInputVec[inputIndex].shape[0] = inferOutput->onputDataInfo[inputIndex].dims.n;
        detectionInputVec[inputIndex].shape[1] = inferOutput->onputDataInfo[inputIndex].dims.c;
        detectionInputVec[inputIndex].shape[2] = inferOutput->onputDataInfo[inputIndex].dims.h;
        detectionInputVec[inputIndex].shape[3] = inferOutput->onputDataInfo[inputIndex].dims.w;
        detectionInputVec[inputIndex].shape[4] = 1;
        detectionInputVec[inputIndex].dataType =
            essdkplDataType2DspDataType(inferOutput->onputDataInfo[inputIndex].dataType);
        detectionInputVec[inputIndex].pData.memFd = inferOutput->memFd[inputIndex];
        detectionInputVec[inputIndex].pData.offset = 0;
        detectionInputVec[inputIndex].pData.size = inferOutput->onputDataInfo[inputIndex].dataSize;
        app_debug("%s %d ; %s [%d, %d, %d, %d, %d, %d, %lld, %d]\n",
                  "the rtmpose data dsp type  is : ", (int)(detectionInputVec[inputIndex].dataType),
                  "the input info is : ", inputIndex, detectionInputVec[inputIndex].shape[0],
                  detectionInputVec[inputIndex].shape[1], detectionInputVec[inputIndex].shape[2],
                  detectionInputVec[inputIndex].shape[3], detectionInputVec[inputIndex].shape[4],
                  detectionInputVec[inputIndex].pData.memFd, detectionInputVec[inputIndex].pData.size);
    }

    for (int inputIndex = 0; inputIndex < inputSize; inputIndex++) {
        app_debug("%s %d ; %s [%d, %d, %d, %d, %d, %d, %lld, %d]\n",
                  "the rtmpose data dsp type  is : ", (int)(detectionInputVec[inputIndex].dataType),
                  "the input info is : ", inputIndex, detectionInputVec[inputIndex].shape[0],
                  detectionInputVec[inputIndex].shape[1], detectionInputVec[inputIndex].shape[2],
                  detectionInputVec[inputIndex].shape[3], detectionInputVec[inputIndex].shape[4],
                  detectionInputVec[inputIndex].pData.memFd, detectionInputVec[inputIndex].pData.size);
    }

    // get post output

    int32_t batch_size = static_cast<int32_t>(detectionInputVec[0].shape[0]);
    // resize for batch

    app_debug("dspop size rtmpose %d \n", batch_size);
#if 0
    outputVec.resize(batch);

    for (int i = 0; i < 5; ++i) {
        RtmPosePointInfo point;
        point.x = i*8;
        point.y = point.x;
        point.score = 0.7;
        outputVec[0].push_back(point);
    }
#else
#if 0
    for(int batch =0; batch <  batch_size ; batch ++){

        std::vector<ES_POSE_POINT> currentBatchResult(detectionInputVec[0].shape[1]);
        int32_t size = detectionInputVec[0].shape[1];
        currentBatchResult.resize(size);
        printf(" size %d\n",size);
        int32_t ret = ES_AK_CPU_Simcc(mDevice, detectionInputVec.data(), detectionInputVec.size(), rtmposeParams.scale_x, rtmposeParams.scale_y,
                                        currentBatchResult.data(), &size);
        if (ret != 0) {
            app_error("\n ES_AK_CPU_Simcc error 0x%x\n",ret);
            return ret;
        }

        std::vector<RtmPosePointInfo> tempVec;
        for (const auto& point : currentBatchResult) {
            RtmPosePointInfo newPoint;
            newPoint.x = point.x;
            newPoint.y = point.y;
            newPoint.score = point.score;
            tempVec.push_back(newPoint);
        }
        outputVec.push_back(tempVec);
    }
#else
    for (int batch = 0; batch < batch_size; batch++) {
        std::vector<ES_POSE_POINT> currentBatchResult;
        int32_t ret = rtmPose(detectionInputVec, currentBatchResult, rtmposeParams);
        if (ret != 0) {
            app_error("\n ES_AK_CPU_Simcc error 0x%x\n", ret);
            return ret;
        }

        std::vector<RtmPosePointInfo> tempVec;
        for (const auto &point : currentBatchResult) {
            RtmPosePointInfo newPoint;
            newPoint.x = point.x;
            newPoint.y = point.y;
            newPoint.score = point.score;
            tempVec.push_back(newPoint);
        }
        outputVec.push_back(tempVec);
    }
#endif
#endif

    return ret;
}
app_ret DspOp::detectionPostprocess(CInferOutputMeta *inferOutput, DetectionOutParams &detectionParams,
                                    std::vector<std::vector<DetectionOutput>> &outputVec) {
    int ret = 0;
    std::vector<ES_TENSOR_S> detectionInputVec;
    int inputSize = inferOutput->onputDataInfo.size();
    if (inputSize == 0) {
        app_error(" %s \n", " detection input fd size is 0 ");
        return APP_FAILURE;
    }
    detectionInputVec.resize(inputSize);
    for (int inputIndex = 0; inputIndex < inputSize; inputIndex++) {
        detectionInputVec[inputIndex].shapeDim = 5;
        detectionInputVec[inputIndex].shape[0] = inferOutput->onputDataInfo[inputIndex].dims.n;
        detectionInputVec[inputIndex].shape[1] = inferOutput->onputDataInfo[inputIndex].dims.c;
        detectionInputVec[inputIndex].shape[2] = inferOutput->onputDataInfo[inputIndex].dims.h;
        detectionInputVec[inputIndex].shape[3] = inferOutput->onputDataInfo[inputIndex].dims.w;
        detectionInputVec[inputIndex].shape[4] = 1;
        detectionInputVec[inputIndex].dataType =
            essdkplDataType2DspDataType(inferOutput->onputDataInfo[inputIndex].dataType);
        detectionInputVec[inputIndex].pData.memFd = inferOutput->memFd[inputIndex];
        detectionInputVec[inputIndex].pData.offset = 0;
        detectionInputVec[inputIndex].pData.size = inferOutput->onputDataInfo[inputIndex].dataSize;
        app_debug("%s %d ; %s [%d, %d, %d, %d, %d, %d, %lld, %d]\n",
                  "the detectionInputVec data dsp type  is : ", (int)(detectionInputVec[inputIndex].dataType),
                  "the input info is : ", inputIndex, detectionInputVec[inputIndex].shape[0],
                  detectionInputVec[inputIndex].shape[1], detectionInputVec[inputIndex].shape[2],
                  detectionInputVec[inputIndex].shape[3], detectionInputVec[inputIndex].shape[4],
                  detectionInputVec[inputIndex].pData.memFd, detectionInputVec[inputIndex].pData.size);
    }
    std::sort(detectionInputVec.begin(), detectionInputVec.end(),
              [](ES_TENSOR_S &t1, ES_TENSOR_S &t2) { return t1.shape[2] * t1.shape[3] > t2.shape[2] * t2.shape[3]; });

    for (int inputIndex = 0; inputIndex < inputSize; inputIndex++) {
        app_debug("%s %d ; %s [%d, %d, %d, %d, %d, %d, %lld, %d]\n",
                  "the detectionInputVec data dsp type  is : ", (int)(detectionInputVec[inputIndex].dataType),
                  "the input info is : ", inputIndex, detectionInputVec[inputIndex].shape[0],
                  detectionInputVec[inputIndex].shape[1], detectionInputVec[inputIndex].shape[2],
                  detectionInputVec[inputIndex].shape[3], detectionInputVec[inputIndex].shape[4],
                  detectionInputVec[inputIndex].pData.memFd, detectionInputVec[inputIndex].pData.size);
    }

    ES_TENSOR_S output;
    ES_TENSOR_S outputCount;

    prapareDspOutFd(output, outputCount, detectionParams);

    (*detectionPerformance)->performanceStaticStart();

#if 1
    ES_DETECTION_OUT_CFG stDspCfg = {0};
    prepareDspCFG(stDspCfg, detectionParams);

    ret = ES_AK_DSP_DetectionOut((AK_DEVICE_E)m_dspID, detectionInputVec.data(), detectionInputVec.size(), &output,
                                 &outputCount, (ES_DET_NETWORK_E)(detectionParams.detectionName), &stDspCfg);
#else
    ret = ES_AK_DSP_DetectionOut(detectionInputVec.data(), detectionInputVec.size(), output, outputCount,
                                 (ES_DET_NETWORK_E)(detectionParams.detectionName), detectionParams.anchorNum,
                                 detectionParams.anchorScale.data(), detectionParams.inputWH.H,
                                 detectionParams.inputWH.W, detectionParams.classNum, detectionParams.inputScale.data(),
                                 (ES_NMS_METHOD_E)detectionParams.nmsMethod, (ES_IOU_METHOD_E)detectionParams.iouMethod,
                                 (ES_BOX_TYPE_E)XminYminWH, (ES_BOOL)1, detectionParams.maxBboxPerClass,
                                 detectionParams.maxBboxPerImg, detectionParams.scoreThreshold,
                                 detectionParams.iouThreshold, detectionParams.softnmssigma,
                                 detectionParams.imgOffset.offX, detectionParams.imgOffset.offY);
#endif

    if (ret != ES_AK_SUCCESS) {
        app_error("ES_AK_DSP_DetectionOut failed. ret %x\n", ret);
    }
    (*detectionPerformance)->performanceStaticEnd();

    app_debug(" %s \n", " end ES_DSP_Detection_out ");
    app_debug(" %s %d\n", " ES_DSP_Detection_out ret is ", ret);

    std::vector<int32_t> boxCount;
    extractDspOut(output, outputCount, boxCount, outputVec);

    return APP_SUCCESS;
}

app_ret DspOp::segmentPostprocess(CInferOutputMeta *inferOutput) { return APP_SUCCESS; }
