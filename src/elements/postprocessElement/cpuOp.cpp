#define PL_LOG_ID PL_LOG_POSTPROC
#include "cpuOp.h"

app_ret CpuOp::classifyArgmax(CInferOutputMeta *inferOutput, std::vector<int> &maxIndexVec,
                              std::vector<float> &maxConfidenceVec) {
    if (inferOutput->onputDataInfo.size() != 1) {
        app_error(" %s \n", "the classification output size is not 1 ");
    }

    CDims inputDims = inferOutput->onputDataInfo[0].dims;
    int allDataSize = inputDims.n * inputDims.h * inputDims.w * inputDims.c;
    int oneOutputSize = inputDims.h * inputDims.w * inputDims.c;

    app_debug(" %s %d %s %d\n", " the all batchDataSize is:", allDataSize, "the one img data size is: ", oneOutputSize);

    ES_VOID *pVirAddr = ES_SYS_Mmap(inferOutput->memFd[0], inferOutput->blkSize[0], SYS_CACHE_MODE_NOCACHE);
    app_debug(" %s \n", " ES_SYS_Mmap end ");
    app_debug(" %s %p\n", "the   pVirAddr is ", pVirAddr);

    float *outputdataptr = (float *)pVirAddr;
    float maxData = -10000.0;
    int maxIndex = 0;

    for (int index = 0; index < allDataSize; index++) {
        if (maxData < outputdataptr[index]) {
            maxData = outputdataptr[index];
            maxIndex = index;
        }

        if (0 == (1 + index) % oneOutputSize) {
            app_debug(" %s %d %f\n", " the index and confidence is: ", maxIndex, maxData);
            maxConfidenceVec.push_back(maxData);
            while (maxIndex > oneOutputSize) {
                maxIndex -= oneOutputSize;
            }
            maxIndexVec.push_back(maxIndex);
            maxData = -10000.0;
            maxIndex = 0;
        }
    }

    app_debug(" %s \n", " ES_SYS_Munmap start ");
    ES_S32 unmapRet = ES_SYS_Munmap(pVirAddr, inferOutput->blkSize[0]);
    ES_ASSERT(unmapRet == ES_SUCCESS, "post cpu classifyArgmax failed, the size is %d \n", inferOutput->blkSize[0]);
    app_debug(" %s \n", " ES_SYS_Munmap end ");

    return APP_SUCCESS;
}

app_ret CpuOp::detectionPostprocess(CInferOutputMeta *inferOutput,
                                    std::vector<std::vector<DetectionOutput>> &outputVec) {
    int ret = 0;
    if (inferOutput->onputDataInfo.size() != 2) {
        app_error(" %s \n", "the detectionPostprocess output size is not 2 ");
    }

    ES_U64 detectionOutputFd = inferOutput->memFd[0];
    ES_U64 outputSize = inferOutput->blkSize[0];
    CDims outputDims = inferOutput->onputDataInfo[0].dims;

    ES_U64 detectionCountFd = inferOutput->memFd[1];
    ES_U64 detectionCountSize = inferOutput->blkSize[1];
    CDims countDims = inferOutput->onputDataInfo[1].dims;
    int shapeN = countDims.n;
    // 确保 outputVec 已被正确初始化
    if (outputVec.size() != shapeN) {
        outputVec.resize(shapeN);
        app_debug("Resized outputVec to %zu\n", outputVec.size());
    }

    {
        std::vector<int32_t> perBatchBboxCount;
        app_debug(" %s \n", "  detection outputCount map start ");
        ES_VOID *pCountVirAdr = ES_SYS_Mmap(detectionCountFd, detectionCountSize, SYS_CACHE_MODE_NOCACHE);
        app_debug(" %s %p\n", "  detection outputCount viraddr is : ", pCountVirAdr);
        int32_t *pCount = (int32_t *)pCountVirAdr;
        int iSumBbox = 0;
        for (int icountDex = 0; icountDex < shapeN; icountDex++) {
            if (pCount[icountDex] <= 0) {
                continue;
            }
            perBatchBboxCount.push_back(pCount[icountDex]);
            app_debug(" %s %d\n", " the bbox size is : ", pCount[icountDex]);
            iSumBbox += pCount[icountDex];
        }

        app_debug(" %s %d\n", " the bbox sum size  is : ", iSumBbox);
        app_debug(" %s \n", "detection outputCount unmap start");
        ret = ES_SYS_Munmap(pCountVirAdr, detectionCountSize);
        ES_ASSERT(ret == ES_SUCCESS, "post cpu detectionPostprocess failed, the size is %d\n", detectionCountSize);
        app_debug(" %s %d\n", " detection outputCount Munmap ret is ", ret);
        if (iSumBbox <= 0) {
            return 0;
        }

        app_debug(" %s \n", "  detection out map start ");
        ES_VOID *pVirAddr = ES_SYS_Mmap(detectionOutputFd, outputSize, SYS_CACHE_MODE_NOCACHE);
        app_debug(" %s %p\n", "  detection out viraddr is : ", pVirAddr);
        float *tempOutput = (float *)pVirAddr;
        for (int batch = 0; batch < shapeN; batch++) {
            // 计算当前批次的起始索引
            int startIndex = batch * outputDims.n * outputDims.h * outputDims.w * outputDims.c;
            for (int iOutputIndex = startIndex;
                 iOutputIndex < startIndex + outputDims.n * outputDims.h * outputDims.w * outputDims.c;
                 iOutputIndex += 7) {
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
                app_debug(" %s [%d, %d, %f, %f, %f, %f, %f]\n", "  detection out result is : ", oneOutput.batchID,
                          oneOutput.classID, oneOutput.score, oneOutput.left, oneOutput.top, oneOutput.w, oneOutput.h);
                outputVec[batch].push_back(oneOutput);
            }
        }

        app_debug(" %s \n", "detection out unmap start");
        ret = ES_SYS_Munmap(pVirAddr, outputSize);
        ES_ASSERT(ret == ES_SUCCESS, "post cpu detectionPostprocess failed, the size is : %d\n", outputSize);
        app_debug(" %s %d\n", " detection out Munmap ret is ", ret);
    }

    return APP_SUCCESS;
}
