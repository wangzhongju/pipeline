#define PL_LOG_ID PL_LOG_GRID
#include "videoGridElement.h"

#include <yaml-cpp/yaml.h>

#include "pl_mem_wrap.h"
#include "yaml_parser.h"

// static std::map<ES_U64,  ES_U64> gGridIova;

static VIDEO_FRAME_INFO_S* createVideoFrame(ES_U32 width = 1280, ES_U32 height = 720) {
    // ES_BOOL bBitWidth8 = ES_TRUE;
    PIXEL_FORMAT_E srcParam_pixelFormat = PIXEL_FORMAT_NV12;

    VIDEO_FRAME_INFO_S* videoFrameInfo = (VIDEO_FRAME_INFO_S*)malloc(sizeof(VIDEO_FRAME_INFO_S));
    ES_ASSERT(videoFrameInfo != NULL, "");
    memset(videoFrameInfo, 0, sizeof(VIDEO_FRAME_INFO_S));

    videoFrameInfo->videoFrame.fd = 0;  // sourceData.fd;
    // videoFrameInfo->videoFrame.virAddr[0] = 0;
    // //(ES_U64)(ES_UL)sourceData.pVirAddr;
    videoFrameInfo->poolId = 0;  // sourceData.vbPoolId;//

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
    // videoFrameInfo->videoFrame.compressMode = COMPRESS_MODE_NONE;
    // videoFrameInfo->videoFrame.videoFormat = VIDEO_FORMAT_LINEAR;

    videoFrameInfo->videoFrame.dynamicRange = DYNAMIC_RANGE_NONE;  // ignore
    videoFrameInfo->videoFrame.colorGamut = COLOR_GAMUT_BT709;     // ignore

    // videoFrameInfo->videoFrame.PTS = count * (1000000 / fps); // 1s =
    // 1000000us
    //    videoFrameInfo->videoFrame.timeRef = count * 2;
    return videoFrameInfo;
}
static app_ret videoGridMeta2VoInfo(CVideoGridMeta* pVideogridMeta, VIDEO_FRAME_INFO_S* pFrameInfo) {
    if (NULL == pVideogridMeta || NULL == pFrameInfo) {
        return APP_FAILURE;
    }
    memset(pFrameInfo, 0, sizeof(VIDEO_FRAME_INFO_S));
    pFrameInfo->videoFrame.fd = pVideogridMeta->memFd;
    // pFrameInfo->poolId = ES_VB_Handle2PoolId(pVideogridMeta->memFd);

    pFrameInfo->videoFrame.pixelFormat = pVideogridMeta->data_format;
    pFrameInfo->videoFrame.field = VIDEO_FIELD_FRAME;
    pFrameInfo->videoFrame.dynamicRange = DYNAMIC_RANGE_NONE;
    pFrameInfo->videoFrame.colorGamut = COLOR_GAMUT_BT709;

    pFrameInfo->videoFrame.width = pVideogridMeta->width;
    pFrameInfo->videoFrame.height = pVideogridMeta->height;
    if (pFrameInfo->videoFrame.pixelFormat == PIXEL_FORMAT_NV12) {
        pFrameInfo->videoFrame.stride[0] = pVideogridMeta->stride[0];
        pFrameInfo->videoFrame.stride[1] = pVideogridMeta->stride[1];

        pFrameInfo->videoFrame.offset[0] = 0;
        pFrameInfo->videoFrame.offset[1] = pFrameInfo->videoFrame.stride[0] * pFrameInfo->videoFrame.height;
    } else if (pFrameInfo->videoFrame.pixelFormat == PIXEL_FORMAT_B8G8R8A8) {
        pFrameInfo->videoFrame.stride[0] = pVideogridMeta->stride[0];
        pFrameInfo->videoFrame.stride[1] = pVideogridMeta->stride[1];

        pFrameInfo->videoFrame.offset[0] = 0;
        pFrameInfo->videoFrame.offset[1] = pFrameInfo->videoFrame.stride[0] * pFrameInfo->videoFrame.height;
    } else if (pFrameInfo->videoFrame.pixelFormat == PIXEL_FORMAT_B8G8R8 ||
               pFrameInfo->videoFrame.pixelFormat == PIXEL_FORMAT_R8G8B8) {
        pFrameInfo->videoFrame.stride[0] = pVideogridMeta->stride[0];
        pFrameInfo->videoFrame.stride[1] = pVideogridMeta->stride[1];

        pFrameInfo->videoFrame.offset[0] = 0;
        pFrameInfo->videoFrame.offset[1] = pFrameInfo->videoFrame.stride[0] * pFrameInfo->videoFrame.height;
    } else {
        app_error("pixelformat not support ");
    }
    return APP_SUCCESS;
}

app_ret VideoGridElement::Init() {
    if (m_NextElementVec.size() != 1 || m_PreviousElementVec.size() != 1) {
        return APP_FAILURE;
    }

    parse_config_file(&videoGridParam, m_configFile);

    if (videoGridParam.dumpFlag) {
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
    poolCfgPicPool = ES_VB_INVALID_POOLID;

    for (int i = 0; i < MAX_BATCH_SIZE; i++) {
        mFrameMetas[i] = NULL;
    }

    for (int i = 0; i < 3; i++) {
        rgb[i] = videoGridParam.color[i];
    }
    specialRectNum = videoGridParam.specialrectnum;
    for (int i = 0; i < specialRectNum; i++) {
        specialCol[i] = videoGridParam.specialrow[i];
        specialRow[i] = videoGridParam.specialcol[i];
    }

    gmetaPool = new MetaPool<CVideoGridMeta>(videoGridParam.poolsize);

    multisrcCost = new PerformanceStatic(mName + "_MultiSrc_Cost", PERF_STATIC_SEGMENT);
    dumpCost = new PerformanceStatic(mName + "_Dump_Cost", PERF_STATIC_SEGMENT);
    poolCount = 0;

    return APP_SUCCESS;
}

app_ret VideoGridElement::Wait() { return APP_SUCCESS; }

app_ret VideoGridElement::Finish() {
    delete multisrcCost;
    delete dumpCost;
    delete gmetaPool;
    ES_VPS_Deinit();
    if (poolCount == 2) {
        PL_ES_VB_DestroyPool(poolCfgPicPool);
        PL_ES_VB_DestroyPool(pool);
    } else if (poolCount == 1)
        PL_ES_VB_DestroyPool(pool);

    poolCount = 0;
    freeNumaNode(this, sizeof(VideoGridElement));
    return APP_SUCCESS;
}

app_ret VideoGridElement::perfStat() {
    multisrcCost->performanceStaticReport();
    return APP_SUCCESS;
}

extern "C" CElement* createEsVideoGridElement(const char* name, const char* path, int dieIndex, int startPadIndex) {
    return new (bindNumaNode(dieIndex, sizeof(VideoGridElement))) VideoGridElement(name, path, dieIndex, startPadIndex);
}

app_ret VideoGridElement::Start() {
    VB_POOL_CONFIG_S poolCfg = {0};
    poolCfg.blkCnt = videoGridParam.poolsize;
    poolCfg.blkSize = videoGridParam.sizePerFrame;
    poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    memcpy(poolCfg.mmzName, m_VBName.c_str(), strlen(m_VBName.c_str()));
    // ES_S32 ret = ES_VB_CreatePool(&poolCfg, &pool);
    ES_S32 ret = PL_ES_VB_CreatePool(&poolCfg, &pool, true);
    if (ES_SUCCESS != ret) {
        app_error("%s-%d create pool failed.\n", __FUNCTION__, __LINE__);
        return APP_FAILURE;
    } else
        poolCount += 1;

    ES_U64* memFd = (ES_U64*)malloc(poolCfg.blkCnt * sizeof(ES_U64));
    ES_ASSERT(memFd != NULL, "");
    // text image
    int pic_width = videoGridParam.textPicWidth;
    int pic_height = videoGridParam.textPicHeight;
    string pic_file_path = videoGridParam.textPicPath;
    // app_error("1111111111111111111111111111 %d %d %s
    // \n",pic_width,pic_height,pic_file_path);

    FILE* pic_fp;
    pic_fp = fopen(pic_file_path.c_str(), "r");

    VB_POOL_CONFIG_S poolCfgPic = {0};
    poolCfgPic.blkCnt = 1;
    poolCfgPic.blkSize = pic_width * pic_height * 3 / 2;
    int pic_size = pic_width * pic_height * 3 / 2;
    poolCfgPic.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    memcpy(poolCfgPic.mmzName, m_VBName.c_str(), strlen(m_VBName.c_str()));
    ret = PL_ES_VB_CreatePool(&poolCfgPic, &poolCfgPicPool);
    if (ES_SUCCESS != ret) {
        app_error("%s-%d create poolCfgPicPool failed.\n", __FUNCTION__, __LINE__);
        fclose(pic_fp);
        free(memFd);
        return APP_FAILURE;
    } else
        poolCount += 1;

    VIDEO_FRAME_INFO_S* videoFrameInfo = createVideoFrame(pic_width, pic_height);  // count -> pts
    ret = PL_ES_VB_GetBlock(poolCfgPicPool, pic_size, m_VBName.c_str(), &videoFrameInfo->videoFrame.fd, "testsrc");

    ES_U64 fd = videoFrameInfo->videoFrame.fd;
    ES_U64* pVirAddr = ES_NULL;
    pVirAddr = (ES_U64*)ES_SYS_Mmap(fd, pic_size, SYS_CACHE_MODE_NOCACHE);
    assert(NULL != pVirAddr);

    int tempCount = fread(pVirAddr, 1, pic_size, pic_fp);
    assert(pic_size == tempCount);
    fclose(pic_fp);

    for (int i = 0; i < poolCfg.blkCnt; i++) {
        ES_VOID* pIOVA = ES_NULL;
        ret = PL_ES_VB_GetBlock(pool, poolCfg.blkSize, m_VBName.c_str(), &memFd[i], mName.c_str(), true, &pIOVA);
        VIDEO_FRAME_S* frame = (VIDEO_FRAME_S*)malloc(sizeof(VIDEO_FRAME_S));
        ES_ASSERT(frame != NULL, "");
        memset(frame, 0, sizeof(VIDEO_FRAME_S));
        frame->fd = memFd[i];
        frame->width = videoGridParam.width;
        frame->height = videoGridParam.height;
        frame->pixelFormat = videoGridParam.pixelFormat;
        frame->stride[0] = videoGridParam.stride[0];  // frame->width;
        frame->stride[1] = videoGridParam.stride[1];  // frame->width;
        frame->stride[2] = videoGridParam.stride[2];
        frame->offset[0] = 0;
        frame->offset[1] = frame->stride[0] * frame->height;
        frame->offset[2] = frame->stride[0] * frame->height + frame->stride[1] * frame->height;
        frame->supplement.haeIOVA = (ES_U64)pIOVA;
        ES_U32 color;
        ES_U8* ptr = (ES_U8*)(&color);
        ptr[0] = 0;
        ptr[1] = 0;
        ptr[2] = 0;
        ptr[3] = 255;
        RECT_S rect = {0};
        rect.x = 0;
        rect.y = 0;
        rect.width = frame->width;
        rect.height = frame->height;
#ifdef __RISCV__
        ret = ES_VPS_Fill(frame, &color, 1, &rect, 1, HW_TYPE_HAE);
        if (ret) {
            app_error("%s ES_VPS_Fill failed.\n", __FUNCTION__);
            return APP_FAILURE;
        }

        //
        ptr[0] = rgb[2];
        ptr[1] = rgb[1];
        ptr[2] = rgb[0];
        ptr[3] = 255;
        ES_S32 step_x, step_y;
        step_x = frame->width / videoGridParam.columns;
        step_y = frame->height / videoGridParam.rows;

        ES_S32* index_c = specialCol;
        ES_S32* index_r = specialRow;
        for (int i = 0; i < specialRectNum; i++) {
            /*top*/
            rect.x = (step_x * index_c[i]) / 2 * 2;
            rect.y = (step_y * index_r[i] - 5) / 2 * 2;
            rect.width = step_x / 2 * 2;
            rect.height = 10;
            rect.x = rect.x > 0 ? rect.x : 0;
            rect.y = rect.y > 0 ? rect.y : 0;
            rect.width = (rect.x + rect.width) < frame->width ? rect.width : frame->width - rect.x;
            rect.height = (rect.y + rect.height) < frame->height ? rect.height : frame->height - rect.y;
            ret = ES_VPS_Fill(frame, &color, 1, &rect, 1, HW_TYPE_HAE);
            /*left*/
            rect.x = (step_x * index_c[i] - 5) / 2 * 2;
            rect.y = (step_y * index_r[i]) / 2 * 2;
            rect.width = 10;
            rect.height = step_y / 2 * 2;
            rect.x = rect.x > 0 ? rect.x : 0;
            rect.y = rect.y > 0 ? rect.y : 0;
            rect.width = (rect.x + rect.width) < frame->width ? rect.width : frame->width - rect.x;
            rect.height = (rect.y + rect.height) < frame->height ? rect.height : frame->height - rect.y;
            ret = ES_VPS_Fill(frame, &color, 1, &rect, 1, HW_TYPE_HAE);
            /*bottom*/
            rect.x = (step_x * index_c[i]) / 2 * 2;
            rect.y = (step_y * index_r[i] + step_y - 5) / 2 * 2;
            rect.width = step_x / 2 * 2;
            rect.height = 10;
            rect.x = rect.x > 0 ? rect.x : 0;
            rect.y = rect.y > 0 ? rect.y : 0;
            rect.width = (rect.x + rect.width) < frame->width ? rect.width : frame->width - rect.x;
            rect.height = (rect.y + rect.height) < frame->height ? rect.height : frame->height - rect.y;
            ret = ES_VPS_Fill(frame, &color, 1, &rect, 1, HW_TYPE_HAE);
            /*right*/
            rect.x = (step_x * index_c[i] + step_x - 5) / 2 * 2;
            rect.y = (step_y * index_r[i]) / 2 * 2;
            rect.width = 10;
            rect.height = step_y / 2 * 2;
            rect.x = rect.x > 0 ? rect.x : 0;
            rect.y = rect.y > 0 ? rect.y : 0;
            rect.width = (rect.x + rect.width) < frame->width ? rect.width : frame->width - rect.x;
            rect.height = (rect.y + rect.height) < frame->height ? rect.height : frame->height - rect.y;
            ret = ES_VPS_Fill(frame, &color, 1, &rect, 1, HW_TYPE_HAE);
        }

        //

        // RECT_S pDstRect = {1280,1800,2560,360};		//big image
        // RECT_S pSrcRect = {0, 0, 2560, 360};
        // ret= ES_VPS_MultiSourcesBlit(&videoFrameInfo->videoFrame, 1,
        // &pSrcRect, nullptr,&pDstRect, frame, HW_TYPE_HAE);

        // if(ret)
        // {
        // 	app_error("%s ES_VPS_BitBlit failed.\n", __FUNCTION__);
        // 	return APP_FAILURE;
        // }

#endif
        free(frame);
    }

    for (int i = 0; i < poolCfg.blkCnt; i++) {
        PL_ES_VB_ReleaseBlock(memFd[i]);
    }

    ret = ES_SYS_Munmap(pVirAddr, pic_size);
    ES_ASSERT(ret == ES_SUCCESS, "GRID failed, the size is %d \n", pic_size);

    free(videoFrameInfo);

    ES_VOID* pBackupIOVA = ES_NULL;
    ret = PL_ES_VB_GetBlock(pool, videoGridParam.sizePerFrame, m_VBName.c_str(), &backupFd, mName.c_str(), true,
                            &pBackupIOVA);
    backupIOVA = (ES_U64)pBackupIOVA;
    if (ret) {
        app_error("%s get a block from pool %d failed.", __FUNCTION__, pool);
        return APP_FAILURE;
    }

    free(memFd);
    return APP_SUCCESS;
}

// app_ret VideoGridElement::ProcessData(CBaseMeta* baseMeta, CElement const* previousElement) {
//     app_info("%s-%s-%d-%s in\n", PLLOG_fileName(__FILE__), __func__, __LINE__, mName.c_str());
//     app_ret retVal = APP_SUCCESS;
//     CBatchMeta* batchMeta = (CBatchMeta*)baseMeta;
//     // vector<CObjectMeta*> totalValidObjs;

//     if (baseMeta->eosFlag) {
//         PL_ES_VB_ReleaseBlock(backupFd);
//         dumpCost->performanceStaticReport();
//         printf("videogrid eos !!!\n");
//         return retVal;
//     }
//     if(!m_cpuSetFlag)
//     {
//         // cpu_set_t cpuset;
//         // CPU_ZERO(&cpuset);
//         // CPU_SET(m_cpuID, &cpuset);
//         // pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
//         // getCpuNumaID(__func__);
//         set_thread_affinity(m_dieIndex);
//         set_thread_priority(80);
//         m_cpuSetFlag = true;
//     }

//     int imgNum = batchMeta->getFrameMetaSize();
//     int gridNum = videoGridParam.columns * videoGridParam.rows;

//     CVideoGridMeta* videoGridMeta = gmetaPool->allocate();
//     videoGridMeta->pool = gmetaPool;

//     ES_U64 fd = backupFd;
//     ES_U64 fdIOVA = backupIOVA;
//     ES_VOID *pBackupIOVA = ES_NULL;
//     ES_S32 ret = PL_ES_VB_GetBlock(pool, videoGridParam.sizePerFrame, m_VBName.c_str(), &backupFd, mName.c_str(),
//     true, &pBackupIOVA); backupIOVA = (ES_U64)pBackupIOVA; if (ret) {
//         app_error("%s get a block from pool %d failed.", __FUNCTION__, pool);
//         return APP_FAILURE;
//     }

//     VIDEO_FRAME_S frameIn[MAX_VIDEO_GRP_NUM];
//     VIDEO_FRAME_S frameOut, frameBackup;
//     RECT_S rectSrc[MAX_VIDEO_GRP_NUM];
//     RECT_S rectDst[MAX_VIDEO_GRP_NUM];

//     memset(rectSrc, 0, gridNum * sizeof(RECT_S));
//     memset(rectDst, 0, gridNum * sizeof(RECT_S));
//     memset(frameIn, 0, gridNum * sizeof(VIDEO_FRAME_S));
//     memset(&frameOut, 0, sizeof(VIDEO_FRAME_S));
//     memset(&frameBackup, 0, sizeof(VIDEO_FRAME_S));

//     // src
//     int stepX = videoGridParam.width / videoGridParam.columns;
//     int stepY = videoGridParam.height / videoGridParam.rows;

//     for (int i = 0; i < imgNum; i++) {
//         CFrameMeta* frameMeta = batchMeta->getFrameMeta(i);
//         int idx = frameMeta->padIndex - m_startPadIndex;
//         if(idx < 0)
//         {
//             idx = 0;
//         }

//         CImage* img = frameMeta->images[0];
//         if (frameMeta->images.size() > 1) {
//             img = frameMeta->images[1];
//         }
//         frameIn[i] = img->mPic->videoFrame;
//         videoGridMeta->originFrameMetas.push_back(frameMeta);
//         frameIn[i].privateData = 0;
//         rectDst[i].x = (idx % videoGridParam.columns) * stepX + 2;
//         rectDst[i].y = (idx / videoGridParam.columns) * stepY + 2;
//         rectDst[i].width = stepX - 4;
//         rectDst[i].height = stepY - 4;
//         rectSrc[i].x = 0;
//         rectSrc[i].y = 0;
//         rectSrc[i].width = frameIn[i].width;
//         rectSrc[i].height = frameIn[i].height;
//     }

//     // dst
//     frameOut.width = videoGridParam.width;
//     frameOut.height = videoGridParam.height;
//     frameOut.pixelFormat = videoGridParam.pixelFormat;
//     frameOut.stride[0] = videoGridParam.stride[0];
//     frameOut.stride[1] = videoGridParam.stride[1];
//     frameOut.stride[2] = videoGridParam.stride[2];
//     frameOut.offset[0] = 0;
//     frameOut.offset[1] = frameOut.offset[0] + frameOut.stride[0] * frameOut.height;
//     frameOut.offset[2] = frameOut.offset[1] + frameOut.stride[1] * frameOut.height;
//     frameOut.fd = fd;
//     frameOut.supplement.haeIOVA = fdIOVA;
//     frameOut.privateData = 0;

//     // dst
//     frameBackup.width = videoGridParam.width;
//     frameBackup.height = videoGridParam.height;
//     frameBackup.pixelFormat = videoGridParam.pixelFormat;
//     frameBackup.stride[0] = videoGridParam.stride[0];
//     frameBackup.stride[1] = videoGridParam.stride[1];
//     frameBackup.stride[2] = videoGridParam.stride[2];
//     frameBackup.offset[0] = 0;
//     frameBackup.offset[1] = frameBackup.offset[0] + frameBackup.stride[0] * frameBackup.height;
//     frameBackup.offset[2] = frameBackup.offset[1] + frameBackup.stride[1] * frameBackup.height;
//     frameBackup.fd = backupFd;
//     frameBackup.supplement.haeIOVA = backupIOVA;
//     frameBackup.privateData = 0;

//     multisrcCost->performanceStaticStart();
// #ifdef __RISCV__
//     if (imgNum > 0) {
//         auto start0 = std::chrono::high_resolution_clock::now();
//         ret = ES_VPS_MultiSourcesBlit(frameIn, imgNum, rectSrc, NULL, rectDst, &frameOut, HW_TYPE_HAE);
//         auto end11 = std::chrono::high_resolution_clock::now();
//         std::chrono::duration<double, std::milli> duration11 = end11 - start0;
//         //std::cout<<"the videoGrid cost 0 time is : "<<duration11.count()<<std::endl;
//     }

//     if (1) {
//         RECT_S rect = {0, 0, videoGridParam.width, videoGridParam.height};
//         auto start0 = std::chrono::high_resolution_clock::now();
//         ret = ES_VPS_MultiSourcesBlit(&frameOut, 1, &rect, NULL, &rect, &frameBackup, HW_TYPE_HAE);
//         auto end10 = std::chrono::high_resolution_clock::now();
//         std::chrono::duration<double, std::milli> duration10 = end10 - start0;
//         //std::cout<<"the videoGrid cost 1 time is : "<<duration10.count()<<std::endl;
//     }
// #endif
//     multisrcCost->performanceStaticEnd();

//     videoGridMeta->memFd = fd;
//     videoGridMeta->data_format = videoGridParam.pixelFormat;
//     videoGridMeta->data_size = videoGridParam.sizePerFrame;
//     videoGridMeta->width = videoGridParam.width;
//     videoGridMeta->height = videoGridParam.height;
//     videoGridMeta->stride[0] = videoGridParam.stride[0];
//     videoGridMeta->stride[1] = videoGridParam.stride[1];
//     videoGridMeta->stride[2] = videoGridParam.stride[2];
//     videoGridMeta->rows = videoGridParam.rows;
//     videoGridMeta->cols = videoGridParam.columns;
//     videoGridMeta->gridPic = new VIDEO_FRAME_INFO_S;
//     videoGridMeta2VoInfo(videoGridMeta, videoGridMeta->gridPic);
//     videoGridMeta->gridPic->poolId = pool;
//     videoGridMeta->gridPic->modId = ES_ID_VB;
//     batchMeta->videoGrid = videoGridMeta;

//     if (dumpFp != NULL) {
//         dumpCost->performanceStaticStart();
//         ES_U64* pVirAddr = (ES_U64*)ES_SYS_Mmap(videoGridMeta->memFd, videoGridMeta->data_size,
//         SYS_CACHE_MODE_NOCACHE); fwrite(pVirAddr, 1, videoGridMeta->data_size, dumpFp); fflush(dumpFp);
//         ES_SYS_Munmap(pVirAddr, videoGridMeta->data_size);
//         dumpCost->performanceStaticEnd();
//     }

//     // batchMeta->clearFrameMata();

//     app_info("%s-%s-%d-%s out\n", PLLOG_fileName(__FILE__), __func__, __LINE__, mName.c_str());

//     return retVal;
// }

app_ret VideoGridElement::ProcessData(CBaseMeta* baseMeta, CElement const* previousElement) {
    app_info("%s-%s-%d-%s in\n", PLLOG_fileName(__FILE__), __func__, __LINE__, mName.c_str());
    app_ret retVal = APP_SUCCESS;
    CBatchMeta* batchMeta = (CBatchMeta*)baseMeta;
    // vector<CObjectMeta*> totalValidObjs;

    if (baseMeta->eosFlag) {
        PL_ES_VB_ReleaseBlock(backupFd);
        dumpCost->performanceStaticReport();
        printf("videogrid eos !!!\n");
        return retVal;
    }
    if (!m_cpuSetFlag) {
        // cpu_set_t cpuset;
        // CPU_ZERO(&cpuset);
        // CPU_SET(m_cpuID, &cpuset);
        // pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        // getCpuNumaID(__func__);
        set_thread_affinity(m_dieIndex);
        set_thread_priority(80);
        m_cpuSetFlag = true;
    }

    int imgNum = batchMeta->getFrameMetaSize();
    int gridNum = videoGridParam.columns * videoGridParam.rows;

    CVideoGridMeta* videoGridMeta = gmetaPool->allocate();
    videoGridMeta->pool = gmetaPool;

    ES_U64 fd = backupFd;
    ES_U64 fdIOVA = backupIOVA;

    ES_S32 ret = APP_SUCCESS;

    VIDEO_FRAME_S frameIn[MAX_VIDEO_GRP_NUM];
    VIDEO_FRAME_S frameOut, frameBackup;
    RECT_S rectSrc[MAX_VIDEO_GRP_NUM];
    RECT_S rectDst[MAX_VIDEO_GRP_NUM];

    memset(rectSrc, 0, gridNum * sizeof(RECT_S));
    memset(rectDst, 0, gridNum * sizeof(RECT_S));
    memset(frameIn, 0, gridNum * sizeof(VIDEO_FRAME_S));
    memset(&frameOut, 0, sizeof(VIDEO_FRAME_S));
    memset(&frameBackup, 0, sizeof(VIDEO_FRAME_S));

    // src
    int stepX = videoGridParam.width / videoGridParam.columns;
    int stepY = videoGridParam.height / videoGridParam.rows;
    ulong time = 0;
    for (int i = 0; i < imgNum; i++) {
        CFrameMeta* frameMeta = batchMeta->getFrameMeta(i);
        int idx = frameMeta->padIndex - m_startPadIndex;
        if (idx < 0) {
            idx = 0;
        }

        // CImage* img = frameMeta->images[0];
        // if (frameMeta->images.size() > 1) {
        //     img = frameMeta->images[1];
        // }
        int chennelID = videoGridParam.channelID;
        CImage* img = frameMeta->images[chennelID];
        app_debug("videoGrid use channel ID %d to draw objs\n", chennelID);
        frameIn[i] = img->mPic->videoFrame;
        videoGridMeta->originFrameMetas.push_back(frameMeta);
        frameIn[i].privateData = 0;
        rectDst[i].x = (idx % videoGridParam.columns) * stepX + 2;
        rectDst[i].y = (idx / videoGridParam.columns) * stepY + 2;
        rectDst[i].width = stepX - 4;
        rectDst[i].height = stepY - 4;
        rectSrc[i].x = 0;
        rectSrc[i].y = 0;
        rectSrc[i].width = frameIn[i].width;
        rectSrc[i].height = frameIn[i].height;
        time = esclock() / 1000;
        app_debug(" imgNum %d  i %d index %d  \n", imgNum, i, frameMeta->index);
    }

    app_debug("%s %d  time %lld\n", "the batchMeta batchIndex is : ", batchMeta->batchIndex,
              (time - batchMeta->creationTime));

    // dst
    frameOut.width = videoGridParam.width;
    frameOut.height = videoGridParam.height;
    frameOut.pixelFormat = videoGridParam.pixelFormat;
    frameOut.stride[0] = videoGridParam.stride[0];
    frameOut.stride[1] = videoGridParam.stride[1];
    frameOut.stride[2] = videoGridParam.stride[2];
    frameOut.offset[0] = 0;
    frameOut.offset[1] = frameOut.offset[0] + frameOut.stride[0] * frameOut.height;
    frameOut.offset[2] = frameOut.offset[1] + frameOut.stride[1] * frameOut.height;
    frameOut.fd = fd;
    frameOut.supplement.haeIOVA = fdIOVA;
    frameOut.privateData = 0;

    // dst
    frameBackup.width = videoGridParam.width;
    frameBackup.height = videoGridParam.height;
    frameBackup.pixelFormat = videoGridParam.pixelFormat;
    frameBackup.stride[0] = videoGridParam.stride[0];
    frameBackup.stride[1] = videoGridParam.stride[1];
    frameBackup.stride[2] = videoGridParam.stride[2];
    frameBackup.offset[0] = 0;
    frameBackup.offset[1] = frameBackup.offset[0] + frameBackup.stride[0] * frameBackup.height;
    frameBackup.offset[2] = frameBackup.offset[1] + frameBackup.stride[1] * frameBackup.height;
    frameBackup.fd = backupFd;
    frameBackup.supplement.haeIOVA = backupIOVA;
    frameBackup.privateData = 0;

    multisrcCost->performanceStaticStart();
#ifdef __RISCV__
    if (imgNum > 0) {
        auto start0 = std::chrono::high_resolution_clock::now();
        ret = ES_VPS_MultiSourcesBlit(frameIn, imgNum, rectSrc, NULL, rectDst, &frameOut, HW_TYPE_HAE);
        if (ret != 0) {
            app_error(" ES_VPS_MultiSourcesBlit error 0x%x\n", ret);
        }
        auto end11 = std::chrono::high_resolution_clock::now();
        std::chrono::duration<double, std::milli> duration11 = end11 - start0;
        // std::cout<<"the videoGrid cost 0 time is : "<<duration11.count()<<std::endl;
    }

    // if (1) {
    //     RECT_S rect = {0, 0, videoGridParam.width, videoGridParam.height};
    //     auto start0 = std::chrono::high_resolution_clock::now();
    //     ret = ES_VPS_MultiSourcesBlit(&frameOut, 1, &rect, NULL, &rect, &frameBackup, HW_TYPE_HAE);
    //     auto end10 = std::chrono::high_resolution_clock::now();
    //     std::chrono::duration<double, std::milli> duration10 = end10 - start0;
    //     //std::cout<<"the videoGrid cost 1 time is : "<<duration10.count()<<std::endl;
    // }
#endif
    multisrcCost->performanceStaticEnd();

    videoGridMeta->memFd = fd;
    videoGridMeta->data_format = videoGridParam.pixelFormat;
    videoGridMeta->data_size = videoGridParam.sizePerFrame;
    videoGridMeta->width = videoGridParam.width;
    videoGridMeta->height = videoGridParam.height;
    videoGridMeta->stride[0] = videoGridParam.stride[0];
    videoGridMeta->stride[1] = videoGridParam.stride[1];
    videoGridMeta->stride[2] = videoGridParam.stride[2];
    videoGridMeta->rows = videoGridParam.rows;
    videoGridMeta->cols = videoGridParam.columns;
    videoGridMeta->gridPic = new VIDEO_FRAME_INFO_S;
    videoGridMeta2VoInfo(videoGridMeta, videoGridMeta->gridPic);
    videoGridMeta->gridPic->poolId = pool;
    videoGridMeta->gridPic->modId = ES_ID_VB;
    batchMeta->videoGrid = videoGridMeta;
    app_debug(" fd %ld >>>>>>>> \n", fd);

    if (dumpFp != NULL) {
        dumpCost->performanceStaticStart();
        ES_U64* pVirAddr = (ES_U64*)ES_SYS_Mmap(videoGridMeta->memFd, videoGridMeta->data_size, SYS_CACHE_MODE_NOCACHE);
        fwrite(pVirAddr, 1, videoGridMeta->data_size, dumpFp);
        fflush(dumpFp);
        ES_SYS_Munmap(pVirAddr, videoGridMeta->data_size);
        dumpCost->performanceStaticEnd();
    }

    // batchMeta->clearFrameMata();

    app_info("%s-%s-%d-%s out\n", PLLOG_fileName(__FILE__), __func__, __LINE__, mName.c_str());

    return retVal;
}