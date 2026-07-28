#define PL_LOG_ID PL_LOG_PREPROC
#include "preProcElement.h"

#include <yaml-cpp/yaml.h>

#include <chrono>

#include "pl_mem_wrap.h"
#include "yaml_parser.h"

// static std::map<ES_U64,  ES_U64> gPreIova;

static VIDEO_FRAME_INFO_S *createVideoFrame(ES_U32 width = 1920, ES_U32 height = 1080) {
    PIXEL_FORMAT_E srcParam_pixelFormat = PIXEL_FORMAT_NV12;

    VIDEO_FRAME_INFO_S *videoFrameInfo = (VIDEO_FRAME_INFO_S *)malloc(sizeof(VIDEO_FRAME_INFO_S));
    ES_ASSERT(videoFrameInfo != NULL, "");
    memset(videoFrameInfo, 0, sizeof(VIDEO_FRAME_INFO_S));

    videoFrameInfo->videoFrame.fd = 0;  // sourceData.fd;
    videoFrameInfo->poolId = 0;         // sourceData.vbPoolId;//

    videoFrameInfo->videoFrame.stride[0] = width;
    videoFrameInfo->videoFrame.stride[1] = width;

    videoFrameInfo->videoFrame.width = width;
    videoFrameInfo->videoFrame.height = height;

    ES_U32 lStride = width;
    videoFrameInfo->videoFrame.offset[0] = 0;
    videoFrameInfo->videoFrame.stride[0] = lStride;

    ES_U32 lumaSize = lStride * height;
    ES_U32 cStride = lStride;
    ES_U32 chrmSize = (cStride * height) >> 2;
    videoFrameInfo->videoFrame.offset[1] = lumaSize;
    videoFrameInfo->videoFrame.offset[2] = lumaSize + chrmSize;
    videoFrameInfo->videoFrame.stride[1] = cStride;
    videoFrameInfo->videoFrame.stride[2] = cStride;

    videoFrameInfo->videoFrame.pixelFormat = srcParam_pixelFormat;
    videoFrameInfo->videoFrame.field = VIDEO_FIELD_FRAME;

    videoFrameInfo->videoFrame.dynamicRange = DYNAMIC_RANGE_NONE;  // ignore
    videoFrameInfo->videoFrame.colorGamut = COLOR_GAMUT_BT709;     // ignore

    return videoFrameInfo;
}

static float dstrect_calculate(PREPROC_PARAM_S *preProcParam, RECT_S *srcRect, RECT_S *dstRect) {
    float aspectRatio = 1;
    int dstwidth = preProcParam->dims.w;
    int dstheight = preProcParam->dims.h;

    if (preProcParam->aspectRatioParam.enable) {
        int srcrect_w = srcRect->width;
        int srcrect_h = srcRect->height;
        float ratio_w = (float)srcrect_w / dstwidth;
        float ratio_h = (float)srcrect_h / dstheight;
        aspectRatio = ratio_w / ratio_h;
        if (ratio_w > ratio_h) {
            dstRect->x = 0;
            dstRect->width = dstwidth;
            dstRect->y = dstheight / 2 - srcrect_h / ratio_w / 2;
            dstRect->height = srcrect_h / ratio_w;
        } else {
            dstRect->x = dstwidth / 2 - srcrect_w / ratio_h / 2;
            dstRect->width = srcrect_w / ratio_h;
            dstRect->y = 0;
            dstRect->height = dstheight;
        }
    } else {
        dstRect->x = 0;
        dstRect->width = dstwidth;
        dstRect->y = 0;
        dstRect->height = dstheight;
    }
    dstRect->x = dstRect->x / 2 * 2;
    dstRect->y = dstRect->y / 2 * 2;
    dstRect->width = dstRect->width / 2 * 2;
    dstRect->height = dstRect->height / 2 * 2;
    return aspectRatio;
}

// app_ret PreProcElement::check_valid_frame(ES_S32 index)
// {
// 	for(int i = 0; i < preProcParam.interval[1]; i++)
// 	{
// 		if(validFrameIndex[i] == index)
// 		{
// 			return APP_SUCCESS;
// 		}
// 	}
// 	return APP_FAILURE;
// }

app_ret PreProcElement::check_valid_frame(ES_S32 index, ES_S32 padIndex) {
    int tmp = index * preProcParam.interval[1] / preProcParam.interval[0];

    if (changeCnt[padIndex] == tmp) {
        return APP_FAILURE;
    }
    changeCnt[padIndex] = tmp;
    return APP_SUCCESS;
}

app_ret PreProcElement::Init() {
    if (m_NextElementVec.size() != 1 || m_PreviousElementVec.size() != 1) {
        return APP_FAILURE;
    }

    parse_config_file(&preProcParam, m_configFile);

    if (preProcParam.dumpFlag) {
        string tmp = mName + "_dump.raw";
        dumpFp = fopen(tmp.c_str(), "wb");
    } else {
        dumpFp = NULL;
    }

    // /ES_VPS_Deinit();
    ES_S32 ret = ES_VPS_Init();
    if (ret != ES_SUCCESS) {
        app_error("ES_VPS_Init failed ret:%#X\n", ret);
        return ES_FAILURE;
    }

    // VPS_PROPERTY_E property = VPS_PROPERTY_ENGINE_TYPE;
    // HWTYPE_E pParam = HW_TYPE_HAE;
    // ret = ES_VPS_SetProperty(property, (ES_VOID *)&pParam);
    // if (ret) {
    // 	app_error("ES_VPS_SetProperty failed ret\n");
    //     return APP_SUCCESS;
    // }
    pool = ES_VB_INVALID_POOLID;
    poolCount = 0;

    // validFrameIndex = (ES_S32 *)malloc(preProcParam.interval[1] *
    // sizeof(ES_S32)); ES_FLOAT step = (ES_FLOAT)preProcParam.interval[0] /
    // preProcParam.interval[1]; for(int i = 0; i < preProcParam.interval[1];
    // i++)
    // {
    // 	validFrameIndex[i] = (ES_S32)(step * i);
    // }
    memset(changeCnt, -1, sizeof(int) * MAX_VIDEO_GRP_NUM);
    std::string metaName = "die_" + std::to_string(m_dieIndex) + "_PreProcess_" + mName;
    premetaPool = new MetaPool<CPreprocessMeta>(preProcParam.out_pool_size, m_dieIndex, metaName);

    normalCost = new PerformanceStatic(mName + "_ES_VPS_Normalization_Cost", PERF_STATIC_SEGMENT);
    dumpCost = new PerformanceStatic(mName + "_Dump_Cost", PERF_STATIC_SEGMENT);
    onlyNormalCost = new PerformanceStatic("only_2D_Dump_Cost", PERF_STATIC_SEGMENT);
    onlyNormalCost2 = new PerformanceStatic("only_2D_Dump_Cost2", PERF_STATIC_SEGMENT);
    onlyNormalCost3 = new PerformanceStatic("only_2D_Dump_Cost3", PERF_STATIC_SEGMENT);

    return APP_SUCCESS;
}

void PreProcElement::releaseIOVA() {
    static bool bcpuSetFlag = false;
    if (!bcpuSetFlag) {
        set_thread_affinity(m_dieIndex);
        bcpuSetFlag = true;
    }
    while (true) {
        CBaseMeta *baseMeta = outputBaseMetaQueue.pop();
        if (baseMeta->eosFlag) {
            releaseIOVAQueue.push_back(baseMeta);
            m_exitFlag = true;
            return;
        }
        CBatchMeta *batchMeta = (CBatchMeta *)baseMeta;
        std::for_each(batchMeta->mBatchedImgs.begin(), batchMeta->mBatchedImgs.end(),
                      [](CPreprocessMeta *tempPre) { ES_VB_FreeIOVA(VB_UID_HAE, tempPre->memFd); });
        releaseIOVAQueue.push_back(baseMeta);
    }
    return;
}

void PreProcElement::prepareIOVA() {
    static bool bcpuSetFlag = false;
    if (!bcpuSetFlag) {
        set_thread_affinity(m_dieIndex);
        bcpuSetFlag = true;
    }
    do {
        ES_U64 fd = 0;
        ES_S32 ret = PL_ES_VB_GetBlock(pool, preProcParam.size_per_batch, m_VBName.c_str(), &fd, mName.c_str());
        // ES_S32 ret = ES_VB_GetBlock(pool, preProcParam.size_per_batch, m_VBName.c_str(), &fd);
        if (ret != ES_SUCCESS) {
            if (m_exitFlag) {
                break;
            }
            usleep(10);
            continue;
        }

        ES_VOID *pIOVA = ES_NULL;
        // ret = ES_VB_AllocIOVA(fd, VB_UID_HAE, &pIOVA);
        m_preIOVAFd.push_back(IovaData{.fd = fd, .pIOVA = ES_U64(pIOVA)});

    } while (!m_exitFlag);
    return;
}

#if 0
// void PreProcElement::process()
// {
//     static bool bcpuSetFlag = false;
//     if (!bcpuSetFlag)
//     {
//         set_thread_affinity(m_dieIndex);
//         set_thread_priority(80);
//         bcpuSetFlag = true;
//     }

//     while (1)
//     {
//         app_ret retVal = APP_SUCCESS;
//         CBaseMeta *baseMeta = inputBaseMetaQueue.pop();
//         CBatchMeta *batchMeta = (CBatchMeta *)baseMeta;
//         vector<CObjectMeta *> totalValidObjs;
//         int channelId = preProcParam.channelId;
//         app_info("%s-%s-%d-%s in\n", PLLOG_fileName(__FILE__), __func__, __LINE__, mName.c_str());

//         if (baseMeta->eosFlag)
//         {
//             outputBaseMetaQueue.push_back(baseMeta);
//             m_exitFlag = true;
//             dumpCost->performanceStaticReport();
//             return;
//         }

//         int imgNum = 0;
//         int index_array[MAX_VIDEO_GRP_NUM] = {0};
//         if (preProcParam.nextInferParam.enable  )
//         {
//             int fsize = batchMeta->getFrameMetaSize();
//             for (int i = 0; i < fsize; i++)
//             {
//                 CFrameMeta *fmeta = batchMeta->getFrameMeta(i);
//                 int osize = fmeta->objs.size();
//                 for (int j = 0; j < osize; j++)
//                 {
//                     CObjectMeta *obj = fmeta->objs[j];
//                     if (checkClassIndex(obj->classId))
//                     {
//                         totalValidObjs.push_back(obj);
//                     }
//                 }
//             }
//             imgNum = totalValidObjs.size();
//         }
//         else
//         {
//             int size = batchMeta->getFrameMetaSize();
//            // ES_ASSERT(size <= 25, "framemeta is too many in batchmeta,size = : %d", size);

//             for (int i = 0; i < size; i++)
//             {
//                 CFrameMeta *frameMeta = batchMeta->getFrameMeta(i);
//                 app_debug("padindex:%d index:%d\n", frameMeta->padIndex, frameMeta->index);
//                 if (check_valid_frame(frameMeta->index, frameMeta->padIndex /* %
//                 preProcParam.interval[0]*/) == APP_SUCCESS)
//                 {
//                     index_array[imgNum] = i;
//                     imgNum++;
//                 }
//             }
//         }

//         int batchsize = preProcParam.dims.n;
//         int batchnum = (imgNum - 1 + batchsize) / batchsize;

//         app_debug("imgnum:%d batchsize:%d batchnum:%d\n", imgNum, batchsize, batchnum);

//         for (int i = 0; i < batchnum; i++)
//         {
//             CPreprocessMeta *preprocessMeta = premetaPool->allocate();
//             preprocessMeta->pool = premetaPool;

//             ES_U64 fd = 0;
//             ES_S32 ret;
//             // ES_S32 ret = PL_ES_VB_GetBlock(pool, preProcParam.size_per_batch,
//             m_VBName.c_str(), &fd, mName.c_str()); IovaData tempIOVA = m_preIOVAFd.pop(); fd =
//             tempIOVA.fd;

//             // if (ret) {
//             //     app_error("%s get a block from pool %d failed.", __FUNCTION__, pool);
//             //     return APP_FAILURE;
//             // }

//             for (int j = 0; j < batchsize; j++)
//             {
//                 auto start0 = std::chrono::high_resolution_clock::now();
//                 normalCost->performanceStaticStart();
//                 VIDEO_FRAME_S *frameIn, *frameOut;
//                 RECT_S rectSrc, rectDst;
//                 memset(&rectSrc, 0, sizeof(RECT_S));
//                 memset(&rectDst, 0, sizeof(RECT_S));

//                 if (j + i * batchsize >= imgNum)
//                 {
//                     break;
//                 }

//                 if (preProcParam.nextInferParam.enable && preProcParam.nextInferParam.crop_enable)
//                 {
//                     CObjectMeta *obj = totalValidObjs[j + i * batchsize];
//                     // need batch
//                     CFrameMeta *frameMeta = obj->parentFrameMeta;
//                     CImage *img = frameMeta->images[channelId];
//                     frameIn = &(img->mPic->videoFrame);
//                     preprocessMeta->originObjMetas.push_back(obj);
//                     if (obj->detectorConfidence > 0)
//                     {
//                         rectSrc.x = obj->detectorBboxInfo.left * frameIn->width;
//                         rectSrc.y = obj->detectorBboxInfo.top * frameIn->height;
//                         rectSrc.width = obj->detectorBboxInfo.width * frameIn->width;
//                         rectSrc.height = obj->detectorBboxInfo.height * frameIn->height;
//                     }
//                     else if (obj->trackerConfidence > 0)
//                     {
//                         rectSrc.x = obj->trackerBboxInfo.left * frameIn->width;
//                         rectSrc.y = obj->trackerBboxInfo.top * frameIn->height;
//                         rectSrc.width = obj->trackerBboxInfo.width * frameIn->width;
//                         rectSrc.height = obj->trackerBboxInfo.height * frameIn->height;
//                     }
//                 }
//                 else
//                 {
//                     CFrameMeta *frameMeta = batchMeta->getFrameMeta(index_array[j + i * batchsize]); // to do
//                     CImage *img = frameMeta->images[channelId];
//                     frameIn = &(img->mPic->videoFrame);
//                     preprocessMeta->originFrameMetas.push_back(frameMeta);
//                     rectSrc.x = 0;
//                     rectSrc.y = 0;
//                     rectSrc.width = frameIn->width;
//                     rectSrc.height = frameIn->height;
//                 }

//                 auto end10 = std::chrono::high_resolution_clock::now();
//                 std::chrono::duration<double, std::milli> duration10 = end10 - start0;
//                 // std::cout<<"the preprocess cost 0 time is : "<<duration10.count()<<std::endl;

//                 VIDEO_FRAME_S tmpFrameOut;
//                 memset(&tmpFrameOut, 0, sizeof(VIDEO_FRAME_S));
//                 frameOut = &tmpFrameOut;
//                 frameOut->width = preProcParam.dims.w;
//                 frameOut->height = preProcParam.dims.h;
//                 frameOut->pixelFormat = preProcParam.pixel_format;
//                 int Bpp = 0;
//                 esquerybpp(frameOut->pixelFormat, &Bpp);
//                 if (preProcParam.data_order == NHWC)
//                 {
//                     frameOut->stride[0] = frameOut->width * Bpp;
//                     frameOut->offset[0] = j * frameOut->width * frameOut->height * Bpp;
//                 }
//                 else if (preProcParam.data_order == NCHW)
//                 {
//                     frameOut->stride[0] = frameOut->width * Bpp / 3;
//                     frameOut->stride[1] = frameOut->width * Bpp / 3;
//                     frameOut->stride[2] = frameOut->width * Bpp / 3;
//                     frameOut->offset[0] = j * frameOut->width * frameOut->height * Bpp;
//                     frameOut->offset[1] = frameOut->offset[0] + frameOut->stride[0] * frameOut->height;
//                     frameOut->offset[2] = frameOut->offset[1] + frameOut->stride[1] * frameOut->height;
//                 }

//                 frameOut->fd = fd;
//                 frameOut->supplement.haeIOVA = tempIOVA.pIOVA;
//                 VPS_NORMALIZATION_PARAMS_S *pParams = NULL;
//                 if (preProcParam.normalizationInfo.bEnable)
//                 {
//                     pParams = &(preProcParam.normalizationInfo.param);
//                 }
//                 // calculate rects
//                 float aspectRatio = dstrect_calculate(&preProcParam, &rectSrc, &rectDst);
//                 preprocessMeta->aspectRatio.push_back(aspectRatio);
//                 app_debug("rectSrc:%d %d %d %d\n", rectSrc.x, rectSrc.y, rectSrc.width, rectSrc.height);
//                 app_debug("rectDst:%d %d %d %d\n", rectDst.x, rectDst.y, rectDst.width, rectDst.height);
//                 ES_U64 privateData = frameIn->privateData;
//                 frameIn->privateData = 0;
//                 frameOut->privateData = 0;

//                 // std::cout<<"the preprocess cost 1 time is : "<<duration11.count()-duration10.count()<<std::endl;

// #ifdef __RISCV__
//                 auto end11 = std::chrono::high_resolution_clock::now();
//                 std::chrono::duration<double, std::milli> duration11 = end11 - start0;
//                 onlyNormalCost->performanceStaticStart();
//                 ret = ES_VPS_Normalization(frameIn, frameOut, pParams, &rectSrc, &rectDst);
//                 onlyNormalCost->performanceStaticEnd();
//                 auto end12 = std::chrono::high_resolution_clock::now();
//                 std::chrono::duration<double, std::milli> duration12 = end12 - end11;
//                 // std::cout<<"the preprocess cost 2 time is : "<<duration12.count()-duration11.count()<<std::endl;
//                 if (ret != ES_SUCCESS)
//                 {
//                     app_error("%s-%d es vps normalization failed.\n", __FUNCTION__, __LINE__);
//                 }
// #endif
//                 frameIn->privateData = privateData;
//                 // padding

//                 if (preProcParam.aspectRatioParam.enable)
//                 {
//                     RECT_S rectPadding_1, rectPadding_2;

//                     if (rectDst.x > 0)
//                     {
//                         rectPadding_1.x = 0;
//                         rectPadding_1.width = rectDst.x;
//                         rectPadding_1.y = 0;
//                         rectPadding_1.height = frameOut->height;

//                         rectPadding_2.x = rectDst.x + rectDst.width;
//                         rectPadding_2.width = frameOut->width - rectPadding_2.x;
//                         rectPadding_2.y = 0;
//                         rectPadding_2.height = frameOut->height;
//                     }
//                     else if (rectDst.y > 0)
//                     {
//                         rectPadding_1.x = 0;
//                         rectPadding_1.width = frameOut->width;
//                         rectPadding_1.y = 0;
//                         rectPadding_1.height = rectDst.y;

//                         rectPadding_2.x = 0;
//                         rectPadding_2.width = frameOut->width;
//                         rectPadding_2.y = rectDst.y + rectDst.height;
//                         rectPadding_2.height = frameOut->height - rectPadding_2.y;
//                     }
//                     else
//                     {
//                         continue;
//                     }
//                     auto end13 = std::chrono::high_resolution_clock::now();
//                     std::chrono::duration<double, std::milli> duration13 = end13 - start0;
//                     // std::cout<<"the preprocess cost 3 time is : "<<duration13.count()-duration12.count()<<std::endl;
// #ifdef __RISCV__
//                     onlyNormalCost2->performanceStaticStart();
//                     ret = ES_VPS_Normalization(mbackGround, frameOut, pParams, &mbackGroundRect, &rectPadding_1);
//                     onlyNormalCost2->performanceStaticEnd();
//                     auto end14 = std::chrono::high_resolution_clock::now();
//                     std::chrono::duration<double, std::milli> duration14 = end14 - end13;
//                     // std::cout<<"the preprocess cost 4 time is : "<<duration14.count()-duration13.count()<<std::endl;
//                     if (ret != ES_SUCCESS)
//                     {
//                         app_error("%s-%d es vps normalization failed.\n", __FUNCTION__, __LINE__);
//                     }
//                     onlyNormalCost3->performanceStaticStart();
//                     ret = ES_VPS_Normalization(mbackGround, frameOut, pParams, &mbackGroundRect, &rectPadding_2);
//                     onlyNormalCost3->performanceStaticEnd();

//                     auto end15 = std::chrono::high_resolution_clock::now();
//                     std::chrono::duration<double, std::milli> duration15 = end15 - end11;
//                     // std::cout<<"the preprocess cost 5 time is : "<<duration15.count()<<std::endl;

//                     if (ret != ES_SUCCESS)
//                     {
//                         app_error("%s-%d es vps normalization failed.\n", __FUNCTION__, __LINE__);
//                     }
// #endif
//                 }

//                 // ES_VPS_UnWrapUserMemory(frameIn);
//                 // ES_VPS_UnWrapUserMemory(frameOut);
//                 normalCost->performanceStaticEnd();
//                 auto end16 = std::chrono::high_resolution_clock::now();
//                 std::chrono::duration<double, std::milli> duration16 = end16 - start0;
//                 // std::cout<<"the preprocess cost 6 time is : "<<duration16.count()<<std::endl;
//             }

//             // preprocessMeta->blk = blk;
//             preprocessMeta->memFd = fd;
//             preprocessMeta->dataOrder = preProcParam.data_order;
//             preprocessMeta->dataType = preProcParam.data_type;
//             preprocessMeta->data_format = preProcParam.pixel_format;
//             preprocessMeta->data_size = preProcParam.size_per_batch;
//             memcpy(&(preprocessMeta->dims), &(preProcParam.dims), sizeof(CDims));
//             preprocessMeta->targetInferIds = preProcParam.target_infer_ids;
//             preprocessMeta->selectClassIds = preProcParam.select_class_ids;

//             int ipreParentObjSize = preprocessMeta->originObjMetas.size();
//             ES_ASSERT(ipreParentObjSize == 0, "the preprocess parent Obj meta size is : %d", ipreParentObjSize);

//             batchMeta->mBatchedImgs.push_back(preprocessMeta);
//             if (dumpFp != NULL)
//             {
//                 dumpCost->performanceStaticStart();
//                 ES_U64 *pVirAddr = (ES_U64 *)ES_SYS_Mmap(preprocessMeta->memFd, preprocessMeta->data_size, SYS_CACHE_MODE_NOCACHE);
//                 fwrite(pVirAddr, 1, preprocessMeta->data_size, dumpFp);
//                 fflush(dumpFp);
//                 ES_SYS_Munmap(pVirAddr, preprocessMeta->data_size);
//                 dumpCost->performanceStaticEnd();
//             }
//         }

//         totalValidObjs.clear();
//         app_info("%s-%s-%d-%s out\n", PLLOG_fileName(__FILE__), __func__, __LINE__, mName.c_str());
//         outputBaseMetaQueue.push_back(batchMeta);
//     }

//     return;
// }

// app_ret PreProcElement::Start() {
//     VB_POOL_CONFIG_S poolCfg = {0};
//     poolCfg.blkCnt = preProcParam.out_pool_size + 1;
//     poolCfg.blkSize = preProcParam.size_per_batch;
//     poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
//     memcpy(poolCfg.mmzName, m_VBName.c_str(), strlen(m_VBName.c_str()));

//     // ES_S32 ret = ES_VB_CreatePool(&poolCfg, &pool);
//     ES_S32 ret = PL_ES_VB_CreatePool(&poolCfg, &pool);
//     if (ES_SUCCESS != ret) {
//         app_error("%s create pool failed.", __FUNCTION__);
//         return APP_FAILURE;
//     }

//     // create background buffer
//     if (preProcParam.aspectRatioParam.enable) {
//         ES_U64 fd = 0;
//         ES_S32 ret = PL_ES_VB_GetBlock(pool, preProcParam.size_per_batch / preProcParam.dims.n, m_VBName.c_str(), &fd, mName.c_str());
//         if (ret) {
//             app_error("%s get a block from pool %d failed.", __FUNCTION__, pool);
//             return APP_FAILURE;
//         }
//         mbackGround = (VIDEO_FRAME_S*)malloc(sizeof(VIDEO_FRAME_S));
//         memset(mbackGround, 0, sizeof(VIDEO_FRAME_S));
//         mbackGround->width = preProcParam.dims.w;
//         mbackGround->height = preProcParam.dims.h;
//         mbackGround->pixelFormat = PIXEL_FORMAT_R8G8B8;
//         mbackGround->stride[0] = mbackGround->width * 3;
//         mbackGround->offset[0] = 0;
//         mbackGround->fd = fd;

//         ES_VOID *pIOVA = ES_NULL;
//         ret = ES_VB_AllocIOVA(mbackGround->fd, VB_UID_HAE, &pIOVA);
//         mbackGround->supplement.haeIOVA = (ES_U64)pIOVA;

//         ES_U32 color;
//         ES_U8* ptr = (ES_U8*)(&color);
//         ptr[0] = 255;
//         ptr[1] = preProcParam.aspectRatioParam.padding_value[0];
//         ptr[2] = preProcParam.aspectRatioParam.padding_value[1];
//         ptr[3] = preProcParam.aspectRatioParam.padding_value[2];
//         mbackGroundRect.x = 0;
//         mbackGroundRect.y = 0;
//         mbackGroundRect.width = mbackGround->width;
//         mbackGroundRect.height = mbackGround->height;
// #ifdef __RISCV__
//         ret = ES_VPS_Fill(mbackGround, &color, 1, &mbackGroundRect, 1, HW_TYPE_HAE);
//         if (ret) {
//             app_error("%s ES_VPS_Fill failed.\n", __FUNCTION__);
//             return APP_FAILURE;
//         }
// #endif
//     }

//     prepareIOVAThread = std::thread(std::mem_fn(&PreProcElement::prepareIOVA), this);
//     releaseIOVAThread = std::thread(std::mem_fn(&PreProcElement::releaseIOVA), this);
//     processThread = std::thread(std::mem_fn(&PreProcElement::process), this);
//     return APP_SUCCESS;
// }
#endif

// 定义 2MB 的字节数
#define TWO_MB (2 * 1024 * 1024)

// 宏定义实现向上取整功能
#define ROUND_UP_TO_TWO_MB(size) (((size) + TWO_MB - 1) / TWO_MB * TWO_MB)
app_ret PreProcElement::Start() {
    VB_POOL_CONFIG_S poolCfg = {0};
    poolCfg.blkCnt = preProcParam.out_pool_size + 1;
    poolCfg.blkSize = (preProcParam.size_per_batch);
    poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    poolCfg.mmzName[m_VBName.copy(poolCfg.mmzName, sizeof(poolCfg.mmzName) - 1)] = 0;

    ES_S32 ret = PL_ES_VB_CreatePool(&poolCfg, &pool, true);
    if (ES_SUCCESS != ret) {
        app_error("%s create pool failed.", __FUNCTION__);
        return APP_FAILURE;
    } else
        poolCount += 1;

    // create background buffer

    if (preProcParam.aspectRatioParam.enable) {
        ES_U64 fd = 0;
        ES_VOID *pIOVA = ES_NULL;
        int Bpp = 0;
        VIDEO_FRAME_S *pBackground = NULL;
        esquerybpp(PIXEL_FORMAT_R8G8B8, &Bpp);

        mbackGround = (VIDEO_FRAME_S *)malloc(sizeof(VIDEO_FRAME_S) * poolCfg.blkCnt * preProcParam.dims.n);
        memset(mbackGround, 0, sizeof(VIDEO_FRAME_S) * poolCfg.blkCnt * preProcParam.dims.n);

        for (int cnt = 0; cnt < poolCfg.blkCnt; cnt++) {
            ES_S32 ret = PL_ES_VB_GetBlock(pool, preProcParam.size_per_batch, m_VBName.c_str(), &fd, mName.c_str(),
                                           true, &pIOVA);
            if (ret) {
                app_error("%s get a block from pool %d failed.", __FUNCTION__, pool);
                return APP_FAILURE;
            }
            for (int batch = 0; batch < preProcParam.dims.n; batch++) {
                pBackground = &mbackGround[cnt * preProcParam.dims.n + batch];
                pBackground->width = preProcParam.dims.w;
                pBackground->height = preProcParam.dims.h;
                pBackground->pixelFormat = PIXEL_FORMAT_R8G8B8;  // default  color for background
                if (preProcParam.data_order == NHWC) {
                    pBackground->stride[0] = pBackground->width * Bpp;
                    pBackground->offset[0] = batch * pBackground->width * pBackground->height * Bpp;
                } else if (preProcParam.data_order == NCHW) {
                    pBackground->stride[0] = pBackground->width;
                    pBackground->stride[1] = pBackground->width;
                    pBackground->stride[2] = pBackground->width;
                    pBackground->offset[0] = batch * pBackground->width * pBackground->height * Bpp;
                    pBackground->offset[1] = pBackground->offset[0] + pBackground->stride[0] * pBackground->height;
                    pBackground->offset[2] = pBackground->offset[1] + pBackground->stride[1] * pBackground->height;
                }
                pBackground->fd = fd;
                pBackground->supplement.haeIOVA = (ES_U64)pIOVA;
                ES_U32 color;
                ES_U8 *ptr = (ES_U8 *)(&color);
                ptr[0] = 255;
                ptr[1] = preProcParam.aspectRatioParam.padding_value[0];
                ptr[2] = preProcParam.aspectRatioParam.padding_value[1];
                ptr[3] = preProcParam.aspectRatioParam.padding_value[2];
                mbackGroundRect.x = 0;
                mbackGroundRect.y = 0;
                mbackGroundRect.width = pBackground->width;
                mbackGroundRect.height = pBackground->height;
#ifdef __RISCV__
                ret = ES_VPS_Fill(pBackground, &color, 1, &mbackGroundRect, 1, HW_TYPE_HAE);
                if (ret) {
                    app_error("%s ES_VPS_Fill failed. cnt %d batch %d\n", __FUNCTION__, cnt, batch);
                    return APP_FAILURE;
                }
#endif
            }
            //  0 is  background
            if (pBackground != &mbackGround[0]) {
                ret = PL_ES_VB_ReleaseBlock(fd);
                if (ret) {
                    app_error("%s PL_ES_VB_ReleaseBlock failed. ret %d  fd %x  cnt %d  \n", __FUNCTION__, ret, fd, cnt);
                    return APP_FAILURE;
                }
            }
        }

        VPS_NORMALIZATION_PARAMS_S *pParams = NULL;
        if (preProcParam.normalizationInfo.bEnable) {
            pParams = &(preProcParam.normalizationInfo.param);

            VIDEO_FRAME_S *frameIn, *frameOut;
            frameIn = &mbackGround[0];
            for (int cnt = 0; cnt < preProcParam.out_pool_size; cnt++) {
                for (int batch = 0; batch < preProcParam.dims.n; batch++) {
                    frameOut = &mbackGround[cnt * preProcParam.dims.n + batch + 1 * preProcParam.dims.n];

#ifdef __RISCV__
                    ret = ES_VPS_Normalization(frameIn, frameOut, pParams, &mbackGroundRect, &mbackGroundRect);
                    if (ret) {
                        app_error("%s ES_VPS_Normalization failed.\n", __FUNCTION__);
                        return ret;
                    }
#endif
                }
            }
        }
    }

    return APP_SUCCESS;
}

#if 0
// app_ret PreProcElement::ProcessData(CBaseMeta* baseMeta, CElement const* previousElement) {
//     if(!m_cpuSetFlag)
//     {
//         set_thread_affinity(m_dieIndex);
//         m_cpuSetFlag = true;
//     }

//     app_ret retVal = APP_SUCCESS;
//     CBatchMeta* batchMeta = (CBatchMeta*)baseMeta;
//     vector<CObjectMeta*> totalValidObjs;
//     int channelId = preProcParam.channelId;
//     app_info("%s-%s-%d-%s in\n", PLLOG_fileName(__FILE__), __func__, __LINE__, mName.c_str());

//     inputBaseMetaQueue.push_back(baseMeta);
//     baseMeta = releaseIOVAQueue.pop();

//     return APP_SUCCESS;

//     int imgNum = 0;
//     int index_array[MAX_VIDEO_GRP_NUM] = {0};
//     if (preProcParam.crop_enabled) {
//         int fsize = batchMeta->getFrameMetaSize();
//         for (int i = 0; i < fsize; i++) {
//             CFrameMeta* fmeta = batchMeta->getFrameMeta(i);
//             int osize = fmeta->objs.size();
//             for (int j = 0; j < osize; j++) {
//                 CObjectMeta* obj = fmeta->objs[j];
//                 if (checkClassIndex(obj->classId)) {
//                     totalValidObjs.push_back(obj);
//                 }
//             }
//         }
//         imgNum = totalValidObjs.size();
//     } else {
//         int size = batchMeta->getFrameMetaSize();
//         ES_ASSERT(size <= 25, "framemeta is too many in batchmeta,size = : %d", size);

//         for (int i = 0; i < size; i++) {
//             CFrameMeta* frameMeta = batchMeta->getFrameMeta(i);
//             app_debug("padindex:%d index:%d\n", frameMeta->padIndex, frameMeta->index);
//             if (check_valid_frame(frameMeta->index, frameMeta->padIndex /* % preProcParam.interval[0]*/) == APP_SUCCESS) {
//                 index_array[imgNum] = i;
//                 imgNum++;
//             }
//         }
//     }

//     int batchsize = preProcParam.dims.n;
//     int batchnum = (imgNum - 1 + batchsize) / batchsize;

//     app_debug("imgnum:%d batchsize:%d batchnum:%d\n", imgNum, batchsize, batchnum);

//     for (int i = 0; i < batchnum; i++) {
//         CPreprocessMeta* preprocessMeta = premetaPool->allocate();
//         preprocessMeta->pool = premetaPool;

//         ES_U64 fd = 0;
//         ES_S32 ret;
//         // ES_S32 ret = PL_ES_VB_GetBlock(pool, preProcParam.size_per_batch, m_VBName.c_str(), &fd, mName.c_str());
//         IovaData tempIOVA = m_preIOVAFd.pop();
//         fd = tempIOVA.fd;

//         // if (ret) {
//         //     app_error("%s get a block from pool %d failed.", __FUNCTION__, pool);
//         //     return APP_FAILURE;
//         // }

//         for (int j = 0; j < batchsize; j++) {
//             auto start0 = std::chrono::high_resolution_clock::now();
//             normalCost->performanceStaticStart();
//             VIDEO_FRAME_S *frameIn, *frameOut;
//             RECT_S rectSrc, rectDst;
//             memset(&rectSrc, 0, sizeof(RECT_S));
//             memset(&rectDst, 0, sizeof(RECT_S));

//             if (j + i * batchsize >= imgNum) {
//                 break;
//             }

//             if (preProcParam.crop_enabled) {
//                 CObjectMeta* obj = totalValidObjs[j + i * batchsize];
//                 CFrameMeta* frameMeta = obj->parentFrameMeta;
//                 CImage* img = frameMeta->images[channelId];
//                 frameIn = &(img->mPic->videoFrame);
//                 preprocessMeta->originObjMetas.push_back(obj);
//                 if (obj->detectorConfidence > 0) {
//                     rectSrc.x = obj->detectorBboxInfo.left * frameIn->width;
//                     rectSrc.y = obj->detectorBboxInfo.top * frameIn->height;
//                     rectSrc.width = obj->detectorBboxInfo.width * frameIn->width;
//                     rectSrc.height = obj->detectorBboxInfo.height * frameIn->height;
//                 } else if (obj->trackerConfidence > 0) {
//                     rectSrc.x = obj->trackerBboxInfo.left * frameIn->width;
//                     rectSrc.y = obj->trackerBboxInfo.top * frameIn->height;
//                     rectSrc.width = obj->trackerBboxInfo.width * frameIn->width;
//                     rectSrc.height = obj->trackerBboxInfo.height * frameIn->height;
//                 }
//             } else {
//                 CFrameMeta* frameMeta = batchMeta->getFrameMeta(index_array[j + i * batchsize]);  // to do
//                 CImage* img = frameMeta->images[channelId];
//                 frameIn = &(img->mPic->videoFrame);
//                 preprocessMeta->originFrameMetas.push_back(frameMeta);
//                 rectSrc.x = 0;
//                 rectSrc.y = 0;
//                 rectSrc.width = frameIn->width;
//                 rectSrc.height = frameIn->height;
//             }

//             auto end10 = std::chrono::high_resolution_clock::now();
//             std::chrono::duration<double, std::milli> duration10 = end10 - start0;
//             //std::cout<<"the preprocess cost 0 time is : "<<duration10.count()<<std::endl;

//             VIDEO_FRAME_S tmpFrameOut;
//             memset(&tmpFrameOut, 0, sizeof(VIDEO_FRAME_S));
//             frameOut = &tmpFrameOut;
//             frameOut->width = preProcParam.dims.w;
//             frameOut->height = preProcParam.dims.h;
//             frameOut->pixelFormat = preProcParam.pixel_format;
//             int Bpp = 0;
//             esquerybpp(frameOut->pixelFormat, &Bpp);
//             if (preProcParam.data_order == NHWC) {
//                 frameOut->stride[0] = frameOut->width * Bpp;
//                 frameOut->offset[0] = j * frameOut->width * frameOut->height * Bpp;
//             } else if (preProcParam.data_order == NCHW) {
//                 frameOut->stride[0] = frameOut->width * Bpp / 3;
//                 frameOut->stride[1] = frameOut->width * Bpp / 3;
//                 frameOut->stride[2] = frameOut->width * Bpp / 3;
//                 frameOut->offset[0] = j * frameOut->width * frameOut->height * Bpp;
//                 frameOut->offset[1] = frameOut->offset[0] + frameOut->stride[0] * frameOut->height;
//                 frameOut->offset[2] = frameOut->offset[1] + frameOut->stride[1] * frameOut->height;
//             }

//             frameOut->fd = fd;
//             frameOut->supplement.haeIOVA = tempIOVA.pIOVA;
//             VPS_NORMALIZATION_PARAMS_S* pParams = NULL;
//             if (preProcParam.normalizationInfo.bEnable) {
//                 pParams = &(preProcParam.normalizationInfo.param);
//             }
//             // calculate rects
//             float aspectRatio = dstrect_calculate(&preProcParam, &rectSrc, &rectDst);
//             preprocessMeta->aspectRatio.push_back(aspectRatio);
//             app_debug("rectSrc:%d %d %d %d\n", rectSrc.x, rectSrc.y, rectSrc.width, rectSrc.height);
//             app_debug("rectDst:%d %d %d %d\n", rectDst.x, rectDst.y, rectDst.width, rectDst.height);
//             ES_U64 privateData = frameIn->privateData;
//             frameIn->privateData = 0;
//             frameOut->privateData = 0;
//             auto end11 = std::chrono::high_resolution_clock::now();
//             std::chrono::duration<double, std::milli> duration11 = end11 - start0;
//             //std::cout<<"the preprocess cost 1 time is : "<<duration11.count()-duration10.count()<<std::endl;

// #ifdef __RISCV__
//             ret = ES_VPS_Normalization(frameIn, frameOut, pParams, &rectSrc, &rectDst);
//             auto end12 = std::chrono::high_resolution_clock::now();
//             std::chrono::duration<double, std::milli> duration12 = end12 - start0;
//             //std::cout<<"the preprocess cost 2 time is : "<<duration12.count()-duration11.count()<<std::endl;
//             if (ret != ES_SUCCESS) {
//                 app_error("%s-%s es vps normalization failed.\n", __FUNCTION__, __LINE__);
//             }
// #endif
//             frameIn->privateData = privateData;
//             // padding
// #if 1
//             if (preProcParam.aspectRatioParam.enable) {
//                 RECT_S rectPadding_1, rectPadding_2;

//                 if (rectDst.x > 0) {
//                     rectPadding_1.x = 0;
//                     rectPadding_1.width = rectDst.x;
//                     rectPadding_1.y = 0;
//                     rectPadding_1.height = frameOut->height;

//                     rectPadding_2.x = rectDst.x + rectDst.width;
//                     rectPadding_2.width = frameOut->width - rectPadding_2.x;
//                     rectPadding_2.y = 0;
//                     rectPadding_2.height = frameOut->height;
//                 } else if (rectDst.y > 0) {
//                     rectPadding_1.x = 0;
//                     rectPadding_1.width = frameOut->width;
//                     rectPadding_1.y = 0;
//                     rectPadding_1.height = rectDst.y;

//                     rectPadding_2.x = 0;
//                     rectPadding_2.width = frameOut->width;
//                     rectPadding_2.y = rectDst.y + rectDst.height;
//                     rectPadding_2.height = frameOut->height - rectPadding_2.y;
//                 } else {
//                     continue;
//                 }
//                 auto end13 = std::chrono::high_resolution_clock::now();
//                 std::chrono::duration<double, std::milli> duration13 = end13 - start0;
//                 //std::cout<<"the preprocess cost 3 time is : "<<duration13.count()-duration12.count()<<std::endl;
// #ifdef __RISCV__
//                 ret = ES_VPS_Normalization(mbackGround, frameOut, pParams, &mbackGroundRect, &rectPadding_1);
//                 auto end14 = std::chrono::high_resolution_clock::now();
//                 std::chrono::duration<double, std::milli> duration14 = end14 - start0;
//                 //std::cout<<"the preprocess cost 4 time is : "<<duration14.count()-duration13.count()<<std::endl;
//                 if (ret != ES_SUCCESS) {
//                     app_error("%s-%s es vps normalization failed.\n", __FUNCTION__, __LINE__);
//                 }
//                 ret = ES_VPS_Normalization(mbackGround, frameOut, pParams, &mbackGroundRect, &rectPadding_2);
//                 auto end15 = std::chrono::high_resolution_clock::now();
//                 std::chrono::duration<double, std::milli> duration15 = end15 - start0;
//                 //std::cout<<"the preprocess cost 5 time is : "<<duration15.count()-duration14.count()<<std::endl;
//                 if (ret != ES_SUCCESS) {
//                     app_error("%s-%s es vps normalization failed.\n", __FUNCTION__, __LINE__);
//                 }
// #endif
//             }
// #endif
//             // ES_VPS_UnWrapUserMemory(frameIn);
//             // ES_VPS_UnWrapUserMemory(frameOut);
//             normalCost->performanceStaticEnd();
//             auto end16 = std::chrono::high_resolution_clock::now();
//             std::chrono::duration<double, std::milli> duration16 = end16 - start0;
//             //std::cout<<"the preprocess cost 6 time is : "<<duration16.count()<<std::endl;
//         }

//         // preprocessMeta->blk = blk;
//         preprocessMeta->memFd = fd;
//         preprocessMeta->dataOrder = preProcParam.data_order;
//         preprocessMeta->dataType = preProcParam.data_type;
//         preprocessMeta->data_format = preProcParam.pixel_format;
//         preprocessMeta->data_size = preProcParam.size_per_batch;
//         memcpy(&(preprocessMeta->dims), &(preProcParam.dims), sizeof(CDims));
//         preprocessMeta->targetInferIds = preProcParam.target_infer_ids;
//         preprocessMeta->selectClassIds = preProcParam.select_class_ids;

//         int ipreParentObjSize = preprocessMeta->originObjMetas.size();
//         ES_ASSERT(ipreParentObjSize == 0, "the preprocess parent Obj meta size is : %d", ipreParentObjSize);

//         batchMeta->mBatchedImgs.push_back(preprocessMeta);
//         if (dumpFp != NULL) {
//             dumpCost->performanceStaticStart();
//             ES_U64* pVirAddr = (ES_U64*)ES_SYS_Mmap(preprocessMeta->memFd, preprocessMeta->data_size, SYS_CACHE_MODE_NOCACHE);
//             fwrite(pVirAddr, 1, preprocessMeta->data_size, dumpFp);
//             fflush(dumpFp);
//             ES_SYS_Munmap(pVirAddr, preprocessMeta->data_size);
//             dumpCost->performanceStaticEnd();
//         }
//     }

//     totalValidObjs.clear();
//     app_info("%s-%s-%d-%s out\n", PLLOG_fileName(__FILE__), __func__, __LINE__, mName.c_str());

//     return retVal;
// }
#endif

void calculate_SrcForDstRatio(RECT_S *rectSrc, int DstWidth, int DstHeight, int frameW, int frameH) {
    // 计算中心点
    int center_x = rectSrc->x + rectSrc->width * 0.5;
    int center_y = rectSrc->y + rectSrc->height * 0.5;

    // 计算缩放尺寸
    int scale_w = rectSrc->width;
    int scale_h = rectSrc->height;

    // 计算宽高比
    float aspect_ratio = static_cast<float>(DstWidth) / DstHeight;

    // 根据宽高比调整缩放尺寸
    if (scale_w > scale_h * aspect_ratio) {
        scale_h = static_cast<int>(scale_w / aspect_ratio);
    } else {
        scale_w = static_cast<int>(scale_h * aspect_ratio);
    }

    // 计算裁剪区域的起始和结束坐标
    int start_x = center_x - scale_w * 0.5;
    int end_x = center_x + scale_w * 0.5 + 1;
    int start_y = center_y - scale_h * 0.5;
    int end_y = center_y + scale_h * 0.5 + 1;

    // 计算新的宽高
    int new_w = end_x - start_x;
    int new_h = end_y - start_y;

    rectSrc->x = start_x;
    rectSrc->y = start_y;
    rectSrc->width = new_w;
    rectSrc->height = new_h;

    // VPS 需要保证2对齐
    rectSrc->x = rectSrc->x / 2 * 2;
    rectSrc->y = rectSrc->y / 2 * 2;
    rectSrc->width = rectSrc->width / 2 * 2;
    rectSrc->height = rectSrc->height / 2 * 2;

    if (rectSrc->x < 0) {
        rectSrc->x = 0;
    }
    if (rectSrc->y < 0) {
        rectSrc->y = 0;
    }
    if (rectSrc->x + rectSrc->width > frameW) {
        rectSrc->width = frameW - rectSrc->x;
    }
    if (rectSrc->y + rectSrc->height > frameH) {
        rectSrc->height = frameH - rectSrc->y;
    }
}

app_ret PreProcElement::ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) {
    if (!m_cpuSetFlag) {
        // cpu_set_t cpuset;
        // CPU_ZERO(&cpuset);
        // CPU_SET(m_cpuID, &cpuset);
        // pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        // getCpuNumaID(__func__);
        set_thread_affinity(m_dieIndex);
        m_cpuSetFlag = true;
    }

    app_ret retVal = APP_SUCCESS;
    CBatchMeta *batchMeta = (CBatchMeta *)baseMeta;
    vector<CObjectMeta *> totalValidObjs;
    int channelId = preProcParam.channelId;
    app_info("%s-%s-%d-%s in\n", PLLOG_fileName(__FILE__), __func__, __LINE__, mName.c_str());

    if (baseMeta->eosFlag) {
        dumpCost->performanceStaticReport();
        app_info("preprocess eos !!!\n");
        return retVal;
    }

    if (batchMeta->mBatchedImgs.size() != 0) {
        batchMeta->releaseBatchedImgs();
    }

    int imgNum = 0;
    int index_array[MAX_VIDEO_GRP_NUM] = {0};
    app_debug("preProcParam.nextInferParam.enable  %d \n", preProcParam.nextInferParam.enable);
    if (preProcParam.nextInferParam.enable) {
        int fsize = batchMeta->getFrameMetaSize();
        for (int i = 0; i < fsize; i++) {
            CFrameMeta *fmeta = batchMeta->getFrameMeta(i);
            int osize = fmeta->objs.size();
            for (int j = 0; j < osize; j++) {
                CObjectMeta *obj = fmeta->objs[j];
                if (checkClassIndex(obj->classId) &&
                    obj->detectorConfidence >= preProcParam.nextInferParam.scoreThreshold) {
                    totalValidObjs.push_back(obj);
                }
            }
        }
        imgNum = totalValidObjs.size();
    } else {
        int size = batchMeta->getFrameMetaSize();
        // ES_ASSERT(size <= 25, "framemeta is too many in batchmeta,size = : %d", size);

        for (int i = 0; i < size; i++) {
            CFrameMeta *frameMeta = batchMeta->getFrameMeta(i);
            app_debug("padindex:%d index:%d\n", frameMeta->padIndex, frameMeta->index);
            if (check_valid_frame(frameMeta->index, frameMeta->padIndex /* % preProcParam.interval[0]*/) ==
                APP_SUCCESS) {
                index_array[imgNum] = i;
                imgNum++;
            }
        }
    }

    int batchsize = preProcParam.dims.n;
    int batchnum = (imgNum - 1 + batchsize) / batchsize;

    int ipreParentObjSize = 0;
    app_debug("preprocess imgnum:%d batchsize:%d batchnum:%d \n", imgNum, batchsize, batchnum);

    for (int i = 0; i < batchnum; i++) {
        CPreprocessMeta *preprocessMeta = premetaPool->allocate();
        preprocessMeta->pool = premetaPool;
        // get dambuffer from user pool
        // VB_BLK blk = ES_VB_GetBlock(pool, preProcParam.size_per_batch,
        // ES_NULL); ES_U64 fd = ES_VB_Handle2Fd(blk);
        ES_U64 fd = 0;
        // ES_S32 ret = ES_VB_GetBlock(pool, preProcParam.size_per_batch,
        // ES_NULL, &fd);

        ES_VOID *pPreIOVA = ES_NULL;
        ES_S32 ret =
            PL_ES_VB_GetBlock(pool, preProcParam.size_per_batch, m_VBName.c_str(), &fd, mName.c_str(), true, &pPreIOVA);
        if (ret) {
            app_error("%s get a block from pool %d failed.", __FUNCTION__, pool);
            return APP_FAILURE;
        }

        for (int j = 0; j < batchsize; j++) {
            auto start0 = std::chrono::high_resolution_clock::now();
            normalCost->performanceStaticStart();
            VIDEO_FRAME_S *frameIn, *frameOut;
            RECT_S rectSrc = {0}, rectDst = {0};

            if (j + i * batchsize >= imgNum) {
                app_info("\n break \n");
                break;
            }

            if (preProcParam.nextInferParam.crop_enable && preProcParam.nextInferParam.enable) {
                CObjectMeta *obj = totalValidObjs[j + i * batchsize];
                CFrameMeta *frameMeta = obj->parentFrameMeta;
                CImage *img = frameMeta->images[channelId];
                frameIn = &(img->mPic->videoFrame);
                preprocessMeta->originObjMetas.push_back(obj);
                if (obj->detectorConfidence > 0) {
                    rectSrc.x = obj->detectorBboxInfo.left * frameIn->width;
                    rectSrc.y = obj->detectorBboxInfo.top * frameIn->height;
                    rectSrc.width = obj->detectorBboxInfo.width * frameIn->width;
                    rectSrc.height = obj->detectorBboxInfo.height * frameIn->height;
                } else if (obj->trackerConfidence > 0) {
                    rectSrc.x = obj->trackerBboxInfo.left * frameIn->width;
                    rectSrc.y = obj->trackerBboxInfo.top * frameIn->height;
                    rectSrc.width = obj->trackerBboxInfo.width * frameIn->width;
                    rectSrc.height = obj->trackerBboxInfo.height * frameIn->height;
                }

                app_debug(" before rectsrc [ %d %d %d %d] \n", rectSrc.x, rectSrc.y, rectSrc.width, rectSrc.height);
                // extend rect
                int offset_x = (int)(rectSrc.width * preProcParam.nextInferParam.extendWidth);
                int offset_y = (int)(rectSrc.height * preProcParam.nextInferParam.extendHeight);

                rectSrc.x = rectSrc.x - offset_x;
                rectSrc.y = rectSrc.y - offset_y;
                rectSrc.width = rectSrc.width + offset_x;
                rectSrc.height = rectSrc.height + offset_y;

                // need  asign to 2
                rectSrc.x = rectSrc.x / 2 * 2;
                rectSrc.y = rectSrc.y / 2 * 2;
                rectSrc.width = rectSrc.width / 2 * 2;
                rectSrc.height = rectSrc.height / 2 * 2;

                if (rectSrc.x < 0) {
                    rectSrc.x = 0;
                }
                if (rectSrc.y < 0) {
                    rectSrc.y = 0;
                }
                if (rectSrc.x + rectSrc.width > frameIn->width) {
                    rectSrc.width = frameIn->width - rectSrc.x;
                }
                if (rectSrc.y + rectSrc.height > frameIn->height) {
                    rectSrc.height = frameIn->height - rectSrc.y;
                }

                app_debug(" after rectsrc [ %d %d %d %d]\n", rectSrc.x, rectSrc.y, rectSrc.width, rectSrc.height);
            } else {
                CFrameMeta *frameMeta = batchMeta->getFrameMeta(index_array[j + i * batchsize]);  // to do
                CImage *img = frameMeta->images[channelId];
                frameIn = &(img->mPic->videoFrame);
                preprocessMeta->originFrameMetas.push_back(frameMeta);
                rectSrc.x = 0;
                rectSrc.y = 0;
                rectSrc.width = frameIn->width;
                rectSrc.height = frameIn->height;
            }
            auto end10 = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> duration10 = end10 - start0;
            // std::cout<<"the preprocess cost 0 time is : "<<duration10.count()<<std::endl;

            VIDEO_FRAME_S tmpFrameOut;
            memset(&tmpFrameOut, 0, sizeof(VIDEO_FRAME_S));
            frameOut = &tmpFrameOut;
            frameOut->width = preProcParam.dims.w;
            frameOut->height = preProcParam.dims.h;
            frameOut->pixelFormat = preProcParam.pixel_format;
            int Bpp = 0;
            esquerybpp(frameOut->pixelFormat, &Bpp);
            if (preProcParam.data_order == NHWC) {
                frameOut->stride[0] = frameOut->width * Bpp;
                frameOut->offset[0] = j * frameOut->width * frameOut->height * Bpp;
            } else if (preProcParam.data_order == NCHW) {
                frameOut->stride[0] = frameOut->width;
                frameOut->stride[1] = frameOut->width;
                frameOut->stride[2] = frameOut->width;
                frameOut->offset[0] = j * frameOut->width * frameOut->height * Bpp;
                frameOut->offset[1] = frameOut->offset[0] + frameOut->stride[0] * frameOut->height;
                frameOut->offset[2] = frameOut->offset[1] + frameOut->stride[1] * frameOut->height;
            }
            frameOut->fd = fd;
            frameOut->supplement.haeIOVA = (ES_U64)pPreIOVA;
            VPS_NORMALIZATION_PARAMS_S *pParams = NULL;
            // if (preProcParam.normalizationInfo.bEnable)
            {
                pParams = &(preProcParam.normalizationInfo.param);
            }

            // calculate rects
            if (preProcParam.aspectRatioParam.keepDstRatio) {
                calculate_SrcForDstRatio(&rectSrc, preProcParam.dims.w, preProcParam.dims.h, frameIn->width,
                                         frameIn->height);
            }

            float aspectRatio = dstrect_calculate(&preProcParam, &rectSrc, &rectDst);
            preprocessMeta->aspectRatio.push_back(aspectRatio);

            float aspectRatioExtW = rectSrc.width * 1.0 / (rectDst.width * frameIn->width);
            preprocessMeta->aspectRatioExtW.push_back(aspectRatioExtW);
            float aspectRatioExtH = rectSrc.height * 1.0 / (rectDst.height * frameIn->height);
            preprocessMeta->aspectRatioExtH.push_back(aspectRatioExtH);
            float aspectRatioExtX = rectSrc.x * 1.0 / frameIn->width;
            preprocessMeta->aspectRatioExtX.push_back(aspectRatioExtX);
            float aspectRatioExtY = rectSrc.y * 1.0 / frameIn->height;
            preprocessMeta->aspectRatioExtY.push_back(aspectRatioExtY);

            app_debug("rectSrc:%d %d %d %d\n", rectSrc.x, rectSrc.y, rectSrc.width, rectSrc.height);
            app_debug("rectDst:%d %d %d %d\n", rectDst.x, rectDst.y, rectDst.width, rectDst.height);
            app_debug("frameIn->width{ %d %d} aspectRatioEx:%f %f %f %f\n", frameIn->width, frameIn->height,
                      aspectRatioExtX, aspectRatioExtY, aspectRatioExtW, aspectRatioExtH);
            ES_U64 privateData = frameIn->privateData;
            frameIn->privateData = 0;
            frameOut->privateData = 0;
            auto end11 = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> duration11 = end11 - start0;
            // std::cout<<"the preprocess cost 1 time is :
            // "<<duration11.count()-duration10.count()<<std::endl;
#ifdef __RISCV__

            if (rectSrc.width == 0 || rectSrc.height == 0) {
                app_debug("\n skip normalizion [w %d h %d]\n", rectSrc.width, rectSrc.height);
            } else {
                // std::lock_guard<std::mutex> lock(mtx);
                ret = ES_VPS_Normalization(frameIn, frameOut, pParams, &rectSrc, &rectDst);
                auto end12 = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double, std::milli> duration12 = end12 - start0;
                // std::cout<<"the preprocess cost 2 time is : "<<duration12.count()-duration11.count()<<std::endl;
                if (ret != ES_SUCCESS) {
                    app_error("%s-%d es vps normalization failed: %x. rectsrc[ %d %d %d %d] dst [%d %d %d %d]\n",
                              __FUNCTION__, __LINE__, ret, rectSrc.x, rectSrc.y, rectSrc.width, rectSrc.height,
                              rectDst.x, rectDst.y, rectDst.width, rectDst.height);
                }
            }
#endif
            frameIn->privateData = privateData;
            // padding
#if 0
            if (preProcParam.aspectRatioParam.enable && m_isAlreadyNormalization.end() == m_isAlreadyNormalization.find(frameOut->fd))
            {
                m_isAlreadyNormalization.insert(frameOut->fd);
                RECT_S rectPadding_1, rectPadding_2;

                if (rectDst.x > 0)
                {
                    rectPadding_1.x = 0;
                    rectPadding_1.width = rectDst.x;
                    rectPadding_1.y = 0;
                    rectPadding_1.height = frameOut->height;

                    rectPadding_2.x = rectDst.x + rectDst.width;
                    rectPadding_2.width = frameOut->width - rectPadding_2.x;
                    rectPadding_2.y = 0;
                    rectPadding_2.height = frameOut->height;
                }
                else if (rectDst.y > 0)
                {
                    rectPadding_1.x = 0;
                    rectPadding_1.width = frameOut->width;
                    rectPadding_1.y = 0;
                    rectPadding_1.height = rectDst.y;

                    rectPadding_2.x = 0;
                    rectPadding_2.width = frameOut->width;
                    rectPadding_2.y = rectDst.y + rectDst.height;
                    rectPadding_2.height = frameOut->height - rectPadding_2.y;
                }
                else
                {
                    continue;
                }
                // printf("rectPadding_1:%d %d %d %d\n", rectPadding_1.x, rectPadding_1.y, rectPadding_1.width, rectPadding_1.height);
                // printf("rectPadding_2:%d %d %d %d\n", rectPadding_2.x, rectPadding_2.y, rectPadding_2.width, rectPadding_2.height);
                auto end13 = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double, std::milli> duration13 = end13 - start0;
                // std::cout<<"the preprocess cost 3 time is : "<<duration13.count()-duration12.count()<<std::endl;
#ifdef __RISCV__
                ret = ES_VPS_Normalization(mbackGround, frameOut, pParams, &mbackGroundRect, &rectPadding_1);
                auto end14 = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double, std::milli> duration14 = end14 - start0;
                // std::cout<<"the preprocess cost 4 time is : "<<duration14.count()-duration13.count()<<std::endl;
                if (ret != ES_SUCCESS)
                {
                    app_error("%s-%d es vps normalization failed, ret %d.\n", __FUNCTION__, __LINE__, ret);
                }
                ret = ES_VPS_Normalization(mbackGround, frameOut, pParams, &mbackGroundRect, &rectPadding_2);
                auto end15 = std::chrono::high_resolution_clock::now();
                std::chrono::duration<double, std::milli> duration15 = end15 - start0;
                // std::cout<<"the preprocess cost 5 time is : "<<duration15.count()-duration14.count()<<std::endl;
                if (ret != ES_SUCCESS)
                {
                    app_error("%s-%d es vps normalization failed.\n", __FUNCTION__, __LINE__);
                }
#endif
            }

#endif
            // ES_VPS_UnWrapUserMemory(frameIn);
            // ES_VPS_UnWrapUserMemory(frameOut);
            normalCost->performanceStaticEnd();
            auto end16 = std::chrono::high_resolution_clock::now();
            std::chrono::duration<double, std::milli> duration16 = end16 - start0;
            // std::cout<<"the preprocess cost 6 time is : "<<duration16.count()<<std::endl;
        }

        // preprocessMeta->blk = blk;
        preprocessMeta->memFd = fd;
        preprocessMeta->dataOrder = preProcParam.data_order;
        preprocessMeta->dataType = preProcParam.data_type;
        preprocessMeta->data_format = preProcParam.pixel_format;
        preprocessMeta->data_size = preProcParam.size_per_batch;
        memcpy(&(preprocessMeta->dims), &(preProcParam.dims), sizeof(CDims));
        preprocessMeta->targetInferIds = preProcParam.target_infer_ids;
        preprocessMeta->selectClassIds = preProcParam.select_class_ids;

        ipreParentObjSize = preprocessMeta->originObjMetas.size();
        // ES_ASSERT(ipreParentObjSize == 0, "the preprocess parent Obj meta size is : %d", ipreParentObjSize);

        if (dumpFp != NULL) {
            dumpCost->performanceStaticStart();
            ES_U64 *pVirAddr =
                (ES_U64 *)ES_SYS_Mmap(preprocessMeta->memFd, preprocessMeta->data_size, SYS_CACHE_MODE_NOCACHE);
            fwrite(pVirAddr, 1, preprocessMeta->data_size, dumpFp);
            fflush(dumpFp);
            ES_SYS_Munmap(pVirAddr, preprocessMeta->data_size);
            dumpCost->performanceStaticEnd();
        }

        batchMeta->mBatchedImgs.push_back(preprocessMeta);
    }

    totalValidObjs.clear();
    app_info("%s-%s-%d-%s out\n", PLLOG_fileName(__FILE__), __func__, __LINE__, mName.c_str());

    return retVal;
}

app_ret PreProcElement::Finish() {
    delete normalCost;
    delete dumpCost;
    delete onlyNormalCost;
    delete onlyNormalCost2;
    delete onlyNormalCost3;
    ES_VPS_Deinit();
    if (mbackGround) {
        PL_ES_VB_ReleaseBlock(mbackGround->fd);
        free(mbackGround);
        mbackGround = NULL;
    }

    while (m_preIOVAFd.size()) {
        IovaData tempIOVA = m_preIOVAFd.pop();
        ES_VB_FreeIOVA(VB_UID_HAE, tempIOVA.fd);
        usleep(1 * 1000);
    }

    if (poolCount) PL_ES_VB_DestroyPool(pool);

    delete premetaPool;
    freeNumaNode(this, sizeof(PreProcElement));
    return APP_SUCCESS;
}

bool PreProcElement::checkClassIndex(int classId) {
    int size = preProcParam.select_class_ids.size();
    for (int i = 0; i < size; i++) {
        if (classId == preProcParam.select_class_ids[i]) return true;
    }
    return false;
}

app_ret PreProcElement::perfStat() {
    normalCost->performanceStaticReport();
    onlyNormalCost->performanceStaticReport();
    onlyNormalCost2->performanceStaticReport();
    onlyNormalCost3->performanceStaticReport();
    return APP_SUCCESS;
}

extern "C" CElement *createEsPreProcessElement(const char *name, const char *config, int dieIndex) {
    return new (bindNumaNode(dieIndex, sizeof(PreProcElement))) PreProcElement(name, config, dieIndex);
}
