#define PL_LOG_ID PL_LOG_TESTSINK
#include "testSinkElement.h"

#include <shared_mutex>

app_ret TestSinkElement::Init() {
    if (m_PreviousElementVec.size() != 1) {
        return APP_FAILURE;
    }
    mFrameCnt = 0;
    return APP_SUCCESS;
}

app_ret TestSinkElement::Start() { return APP_SUCCESS; }

app_ret TestSinkElement::Wait() { return APP_SUCCESS; }

FILE *fp = NULL;
app_ret TestSinkElement::ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) {
    app_debug("testsink ProcessData\n");
    fflush(stdout);
    app_debug("frame cnt:%d\n", mFrameCnt++);
    fflush(stdout);
    app_debug("%s %s %s %d\n", "the name is ", mName.c_str(), "the baseMeta useCount is : ", baseMeta->getUseCount());
    fflush(stdout);
    if (baseMeta->eosFlag) {
        app_info("testsink eos !!!\n");
        fflush(stdout);
        app_debug("%s \n", "  will reduce baseMeta ");
        baseMeta->reduceUseCount();
        app_debug("%s \n", " reduce success ");
        return APP_SUCCESS;
    }

    // if(BATCH_META == baseMeta->mMetaType)
    // {
    //     CBatchMeta *batchMeta = (CBatchMeta*)baseMeta;
    //     int iFrameSize = batchMeta->mImgMetas.size();
    //     for(int iSize=0; iSize<iFrameSize; iSize++)
    //     {
    //         CFrameMeta *frameMeta = batchMeta->mImgMetas[iSize];
    //         int iObjSize  = frameMeta->objs.size();
    //         for(int iObjIndex=0; iObjIndex<iObjSize; iObjIndex++)
    //         {
    //             CObjectMeta *objMeta = frameMeta->objs[iObjIndex];
    //             if(NULL != objMeta->selectedFaces)
    //             {
    //                 if(0 != objMeta->selectedFaces->bgrFaceFrame.fd)
    //                 {

    //                 }

    //                 if(0 != objMeta->selectedFaces->nv12FaceFrame.fd)
    //                 {
    //                     int height =
    //                     objMeta->selectedFaces->nv12FaceFrame.height; int
    //                     size =
    //                     objMeta->selectedFaces->nv12FaceFrame.stride[0] *
    //                     height +
    //                     objMeta->selectedFaces->nv12FaceFrame.stride[1] *
    //                     height / 2;
    //                 }
    //             }
    //         }
    //     }
    // }
#if 0
    //CBatchMeta* batchMeta = (CBatchMeta*)baseMeta;
    CFrameMeta* fmeta = (CFrameMeta*)baseMeta;//(CFrameMeta*)(batchMeta->mImgMetas[0]);
    VIDEO_FRAME_INFO_S* pic = fmeta->images[0]->mPic;
    int stride[4] = {0};
    for(int i = 0; i < 3; i++)
    {
        stride[i] = pic->videoFrame.stride[i];
    }
    int height = pic->videoFrame.height;
    int size = stride[0] * height + stride[1] * height / 2;

    ES_U64* pVirAddr = ES_NULL;
    pVirAddr = (ES_U64*)ES_SYS_Mmap(pic->videoFrame.fd, size, SYS_CACHE_MODE_NOCACHE);
    // VB_POOL poolId = ES_VB_Handle2PoolId(pic->videoFrame.fd);
    // ES_VB_GetBlockVirAddr(poolId, pic->videoFrame.fd, (void**)(&pVirAddr));
    app_debug("pVirAddr:%p\n",pVirAddr);
    if(fp == NULL)
    {
        fp = fopen("./testsink.yuv","wb");
    }
    app_debug("fp:%p\n",fp);
    fwrite((void*)pVirAddr,1,size,fp);
    fflush(fp);
    ES_SYS_Munmap(pVirAddr, size);
    fmeta->reduceUseCount();
#elif 0
    CBatchMeta *batchMeta = (CBatchMeta *)baseMeta;
    CPreprocessMeta *batchedImgs = batchMeta->mBatchedImgs[0];
    app_debug("data_size:%d\n", batchedImgs->data_size);
    ES_U64 *pVirAddr = (ES_U64 *)ES_SYS_Mmap(batchedImgs->memFd, batchedImgs->data_size, SYS_CACHE_MODE_NOCACHE);
    // VB_BLK blk = ES_VB_Fd2Handle(batchedImgs->memFd);
    // ES_VB_GetBlockVirAddr(blk, &pVirAddr);
    // VB_POOL poolId = ES_VB_Handle2PoolId(batchedImgs->memFd);
    // ES_VB_GetBlockVirAddr(poolId, batchedImgs->memFd, (void**)(&pVirAddr));
    app_debug("pVirAddr:%p\n", pVirAddr);
    if (fp == NULL) {
        fp = fopen("./testsink.yuv", "wb");
    }
    app_debug("fp:%p\n", fp);
    fwrite(pVirAddr, 1, batchedImgs->data_size, fp);
    fflush(fp);
    ES_SYS_Munmap(pVirAddr, batchedImgs->data_size);
    batchMeta->reduceUseCount();
#else
    app_debug("%s \n", "  will reduce baseMeta ");
    // auto start0 = std::chrono::high_resolution_clock::now();
    baseMeta->reduceUseCount();
    // auto end1 = std::chrono::high_resolution_clock::now();
    // std::chrono::duration<double, std::milli> duration13 = end1 - start0;
    // std::cout<<"the batchMeta release time is : "<<duration13.count()<<std::endl;
    app_debug("%s \n", " reduce success ");
#endif
    app_debug("%s \n", "  ProcessData  end ");
    return APP_SUCCESS;
}

app_ret TestSinkElement::ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *previousElement) {
    ProcessData(baseMeta, this);
    return APP_SUCCESS;
}

extern "C" CElement *createEsTestSinkElement(const char *name, BASE_ELEMENT_TYPE elementType) {
    return new TestSinkElement(name, elementType);
}
