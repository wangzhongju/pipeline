#define PL_LOG_ID PL_LOG_VENC
#include "vencElement.h"

#include <sys/prctl.h>
#include <yaml-cpp/yaml.h>
extern "C" {
#include <sys/epoll.h>

#include "common/pl_option_enc.h"
#include "es_venc.h"
}

class CVideoPacketMetaVe : public CVideoPacketMeta {
   public:
    CVideoPacketMetaVe() : CVideoPacketMeta() {};
    virtual ~CVideoPacketMetaVe() {};
    void release() {
        if (encVideoPkt != nullptr) {
            ES_S32 retsub = ES_VENC_ReleaseStream(chnId, encVideoPkt);
            if (ES_SUCCESS != retsub) {
                app_error("%s \n", "ES_VENC_ReleaseStream failed!");
                // return APP_FAILURE;
            }
            free(encVideoPkt->pPack);
            delete encVideoPkt;
            encVideoPkt = nullptr;
        }

        delete this;
        // return APP_SUCCESS;
    }
};

bool VencElement::gEncStarted = false;
bool VencElement::gEncFinished = false;
TEST_Client_S_ENC VencElement::gEncClient = {0};

ES_S32 parseYamlCfg(char *m_configFile, TEST_CHN_S_ENC *chnParams) {
    YAML::Node config = YAML::LoadFile(m_configFile);

    // yaml:output
    YAML::Node yaml_encoder = config["encoder"];
    int format_out = yaml_encoder["format_out"].template as<int>();
    // int width_out = yaml_encoder["width_out"].template as<int>();
    // int height_out = yaml_encoder["height_out"].template as<int>();
    int profile = yaml_encoder["profile"].template as<int>();
    int gop_mode = yaml_encoder["gop_mode"].template as<int>();

    int frame_dumpflag = yaml_encoder["dumpframe"].template as<int>();
    chnParams->frameDumpFlag = ES_BOOL(frame_dumpflag > 0);

    YAML::Node yaml_output_goppara = config["encoder"]["gop_para"];
    // int gop_para_QPOffset = yaml_output_goppara["QPOffset"].template
    // as<int>(); float gop_para_QPFactor =
    // yaml_output_goppara["QPFactor"].template as<float>(); int
    // gop_para_interval = yaml_output_goppara["interval"].template as<int>();
    int gop_para_BFrmNum = yaml_output_goppara["BFrmNum"].template as<int>();
    // int gop_para_gopSize = yaml_output_goppara["gopSize"].template as<int>();
    int gop_para_SPInterval = yaml_output_goppara["SPInterval"].template as<int>();
    int gop_para_SPQpDelta = yaml_output_goppara["SPQpDelta"].template as<int>();
    int gop_para_IPQpDelta = yaml_output_goppara["IPQpDelta"].template as<int>();
    int gop_para_BgInterval = yaml_output_goppara["BgInterval"].template as<int>();
    int gop_para_BgQpDelta = yaml_output_goppara["BgQpDelta"].template as<int>();
    int gop_para_ViQpDelta = yaml_output_goppara["ViQpDelta"].template as<int>();
    int gop_para_BQpDelta = yaml_output_goppara["BQpDelta"].template as<int>();

    YAML::Node yaml_output_rcpara = config["encoder"]["rc"];
    int rc_mode = yaml_output_rcpara["rc_mode"].template as<int>();
    int rc_frameRate = yaml_output_rcpara["frameRate"].template as<int>();

    int gop = yaml_encoder["gop"].template as<int>();
    int jpegQFactor = yaml_encoder["jpegQFactor"].template as<int>();

    // pixelFormat
    string pixelFormat = yaml_encoder["pixelFormat"].template as<string>();

    // yaml:filesink ,todo:parse all filesink params
    YAML::Node yaml_filesink = config["filesink"];
    string filename = yaml_filesink["filename"].template as<string>();
    int pack_dumpflag = yaml_filesink["dumppack"].template as<int>();
    chnParams->packDumpFlag = ES_BOOL(pack_dumpflag > 0);

    // verify params and set to pCmdParse
    // handle: filename
    ASSERT(filename.length() < MAX_FILE_NAME_LEN);
    strncpy(chnParams->outputFile, filename.c_str(), MAX_FILE_NAME_LEN);

    // chnParams->width = (ES_U32)width_out;
    // chnParams->height = (ES_U32)height_out;

    // handle: format_out:0:h265, 1:h264, 2:jpeg
    ASSERT(format_out >= 0 && format_out <= 2);
    switch (format_out) {
        case 0: {
            chnParams->type = PT_H265;
            break;
        }
        case 1: {
            chnParams->type = PT_H264;
            break;
        }
        case 2: {
            chnParams->type = PT_JPEG;
            break;
        }
    }
    int bitrate = yaml_encoder["bitrate"].template as<int>();
    chnParams->bitrate = bitrate;

    // handle:pixelFormat
    chnParams->pixelFormat = convertPixelFmt(pixelFormat.c_str());
    ASSERT(chnParams->pixelFormat != PIXEL_FORMAT_BUTT);

    // handle:gopmod
    if (gop_mode < VENC_GOPMODE_BUTT && gop_mode >= 0) {
        chnParams->GOPAttr.GOPMode = VENC_GOP_MODE_E(gop_mode);
    }

    // handle:goppara
    switch (chnParams->GOPAttr.GOPMode) {
        case VENC_GOPMODE_NORMALP: {
            chnParams->GOPAttr.normalP.IPQpDelta = gop_para_IPQpDelta;
            break;
        }

        case VENC_GOPMODE_DUALREF: {
            chnParams->GOPAttr.dualRef.SBInterval = gop_para_SPInterval;
            chnParams->GOPAttr.dualRef.SBQpDelta = gop_para_SPQpDelta;
            chnParams->GOPAttr.dualRef.IPBQpDelta = gop_para_IPQpDelta;
            break;
        }

        case VENC_GOPMODE_SMARTREF: {
            chnParams->GOPAttr.smartRef.BgInterval = gop_para_BgInterval;
            chnParams->GOPAttr.smartRef.BgQpDelta = gop_para_BgQpDelta;
            chnParams->GOPAttr.smartRef.ViQpDelta = gop_para_ViQpDelta;
            break;
        }
        case VENC_GOPMODE_ADVSMARTREF: {
            chnParams->GOPAttr.advSmartRef.BgInterval = gop_para_BgInterval;
            chnParams->GOPAttr.advSmartRef.BgQpDelta = gop_para_BgQpDelta;
            chnParams->GOPAttr.advSmartRef.ViQpDelta = gop_para_ViQpDelta;
            break;
        }
        case VENC_GOPMODE_BIPREDB: {
            chnParams->GOPAttr.bipredB.BFrmNum = gop_para_BFrmNum;
            chnParams->GOPAttr.bipredB.BQpDelta = gop_para_BQpDelta;
            chnParams->GOPAttr.bipredB.IPQpDelta = gop_para_IPQpDelta;
            break;
        }
        case VENC_GOPMODE_LOWDELAYB: {
            chnParams->GOPAttr.lowdelayB.BFrmNum = gop_para_BFrmNum;
            break;
        }
        case VENC_GOPMODE_BUTT: {
            app_error("%s \n", "Error! Please corret encode GOP Mode.");
            break;
        }
    }

    // handle:profile
    chnParams->profile = profile;

    // handle:rc_mode
    ASSERT(rc_mode < VENC_RC_MODE_BUTT && rc_mode >= 0);
    chnParams->rcAttr.rcMode = VENC_RC_MODE_E(rc_mode);
    chnParams->dstFrameRate = rc_frameRate;
    // handle:gop
    ASSERT(gop >= GOP_MIN_VALUE && gop <= GOP_MAX_VALUE);
    switch (chnParams->rcAttr.rcMode) {
        case VENC_RC_MODE_H264CBR:
            chnParams->rcAttr.h264CBR.GOP = gop;
            break;
        case VENC_RC_MODE_H264VBR:
            chnParams->rcAttr.h264VBR.GOP = gop;
            break;
        case VENC_RC_MODE_H264AVBR:
            chnParams->rcAttr.h264AVBR.GOP = gop;
            break;
        case VENC_RC_MODE_H264QVBR:
            chnParams->rcAttr.h264QVBR.GOP = gop;
            break;
        case VENC_RC_MODE_H264CVBR:
            chnParams->rcAttr.h264CVBR.GOP = gop;
            break;
        case VENC_RC_MODE_H264FIXQP:
            chnParams->rcAttr.h264FixQP.GOP = gop;
            break;
        case VENC_RC_MODE_H264QPMAP:
            chnParams->rcAttr.h264QPMap.GOP = gop;
            break;
        case VENC_RC_MODE_H265CBR:
            chnParams->rcAttr.h265CBR.GOP = gop;
            break;
        case VENC_RC_MODE_H265VBR:
            chnParams->rcAttr.h265VBR.GOP = gop;
            break;
        case VENC_RC_MODE_H265AVBR:
            chnParams->rcAttr.h265AVBR.GOP = gop;
            break;
        case VENC_RC_MODE_H265QVBR:
            chnParams->rcAttr.h265QVBR.GOP = gop;
            break;
        case VENC_RC_MODE_H265CVBR:
            chnParams->rcAttr.h265CVBR.GOP = gop;
            break;
        case VENC_RC_MODE_H265FIXQP:
            chnParams->rcAttr.h265FixQP.GOP = gop;
            break;
        case VENC_RC_MODE_H265QPMAP:
            chnParams->rcAttr.h265QPMap.GOP = gop;
            break;
        default:
            app_error("rcMode %d is unsupported. \n", chnParams->rcAttr.rcMode);
            exit(0);
    }

    // handle:jpegQFactor
    chnParams->JPEGParam.qFactor = jpegQFactor;

    return ES_SUCCESS;
}

ES_S32 parseCfg(char *m_configFile, TEST_Client_S_ENC *pEncClient) {
    ES_S32 ret = ES_SUCCESS;
    TEST_CHN_S_ENC chnParams = {0};

    setDefaultParams(&chnParams);
    auto &sharedCounter = SharedCounter::sharedCounter();
    if (sharedCounter.lockSharedMemory() == -1) {
        app_error("VENC parse config get shared mem lock failed!\n");
        return APP_FAILURE;
    }
    try {
        chnParams.grpId += sharedCounter.vencCounterGet();
    } catch (...) {
        sharedCounter.unlockSharedMemory();
        app_error("VENC element try to get counter failed \n");
        throw;
    }
    sharedCounter.unlockSharedMemory();
    parseYamlCfg(m_configFile, &chnParams);

    if (validateAndCreateChns(&chnParams, pEncClient)) {
        app_error("%s \n", "validateAndCreateChns failed");
    }
    return ret;
}

VIDEO_FRAME_INFO_S *createVideoFrameByGrid(CVideoGridMeta *videoGridMeta) {
    // ES_U32 count;
    ES_U32 width = videoGridMeta->width;
    ES_U32 height = videoGridMeta->height;
    // ES_U32 fps = 25;
    // ES_BOOL bBitWidth8 = ES_TRUE;
    PIXEL_FORMAT_E srcParam_pixelFormat = videoGridMeta->data_format;
    assert(srcParam_pixelFormat == PIXEL_FORMAT_NV12 ||
           srcParam_pixelFormat == PIXEL_FORMAT_NV21);  // other should verify

    VIDEO_FRAME_INFO_S *videoFrameInfo = (VIDEO_FRAME_INFO_S *)malloc(sizeof(VIDEO_FRAME_INFO_S));
    memset(videoFrameInfo, 0, sizeof(VIDEO_FRAME_INFO_S));

    videoFrameInfo->videoFrame.fd = videoGridMeta->memFd;  // sourceData.fd;
    // videoFrameInfo->videoFrame.virAddr[0] = 0;
    // //(ES_U64)(ES_UL)sourceData.pVirAddr;
    videoFrameInfo->poolId = 0;  // sourceData.vbPoolId;//

    videoFrameInfo->videoFrame.width = width;
    videoFrameInfo->videoFrame.height = height;

    videoFrameInfo->videoFrame.stride[0] = videoGridMeta->stride[0];
    videoFrameInfo->videoFrame.stride[1] = videoGridMeta->stride[1];
    videoFrameInfo->videoFrame.stride[2] = videoGridMeta->stride[2];
    videoFrameInfo->videoFrame.offset[0] = 0;
    videoFrameInfo->videoFrame.offset[1] = videoGridMeta->stride[0] * height;
    videoFrameInfo->videoFrame.offset[2] = 0;

    videoFrameInfo->videoFrame.pixelFormat = srcParam_pixelFormat;
    videoFrameInfo->videoFrame.field = VIDEO_FIELD_FRAME;

    videoFrameInfo->videoFrame.dynamicRange = DYNAMIC_RANGE_NONE;  // ignore
    videoFrameInfo->videoFrame.colorGamut = COLOR_GAMUT_BT709;     // ignore

    // videoFrameInfo->videoFrame.PTS = count * (1000000 / fps); // 1s =
    // 1000000us
    //    videoFrameInfo->videoFrame.timeRef = count * 2;
    return videoFrameInfo;
}

ES_VOID *getStreamProcByThread(ES_VOID *p) {
    ES_S32 ret = ES_SUCCESS;
    TEST_CHN_S_ENC *pChn = (TEST_CHN_S_ENC *)p;
    VencElement *pVencElement = (VencElement *)pChn->element;
    ES_S32 chnId = pChn->grpId;
    app_info("%s %s \n", pVencElement->mName.c_str(), "Enter thread getStreamProcByThread");

    // set thread name
    char thread_name[64];
    sprintf(thread_name, "getStmThd_%03d", chnId);
    prctl(PR_SET_NAME, (unsigned long)thread_name);

    ES_S32 vencFd = ES_VENC_GetFd(chnId);
    ASSERT(vencFd >= 0);

    // 创建epoll句柄
    int epfd = epoll_create(1);  // only 1 fd
    if (-1 == epfd) {
        app_error("%s %d \n", "epoll_create failed chn: ", chnId);
        return ES_NULL;
    }

    // 注册epoll事件
    struct epoll_event ev;
    ev.data.fd = vencFd;
    ev.events = EPOLLIN;  // 表示对应的文件描述符可以读；

    ret = epoll_ctl(epfd, EPOLL_CTL_ADD, vencFd, &ev);
    if (-1 == ret) {
        app_error("%s %d \n", "epoll_ctl failed chn: ", chnId);
        return ES_NULL;
    }
    struct epoll_event events[1];  // monitor 1 fd
    int nfds = 0;
    int streamCount = 0;
    while (1) {
        streamCount++;
        VENC_STREAM_S *stream = new VENC_STREAM_S();
        pVencElement->getStreamPerformance->performanceStaticStart();
        ES_S32 retsub = ES_SUCCESS;
        VENC_CHN_STATUS_S status = {0};

        // pVencElement->getStreamSelectPerformance->performanceStaticStart();
        if (!pVencElement->sendFrameThdExit) {
            // app_debug("%s \n",  "epoll_wait 100ms ");
            nfds = epoll_wait(epfd, events, 1, 100);  // timeout是超时时间,毫秒
            app_debug("%s \n", "epoll_wait 100ms end");
        } else {
            // app_debug("%s \n",  "epoll_wait 5ms ");
            nfds = epoll_wait(epfd, events, 1, 5);  // timeout 5ms
            app_debug("%s \n", "epoll_wait 5ms end");
        }

        if (nfds < 0) {
            if (errno != EINTR) {  // interrupted system call
                app_error("%s %d %s\n", "select failed!", errno, strerror(errno));
                break;
            }
        }
        // pVencElement->getStreamSelectPerformance->performanceStaticEnd();

        /*******************************************************
         step 2.1 : query how many packs in one-frame stream.
        *******************************************************/
        // pVencElement->getStreamQueryPerformance->performanceStaticStart();
        retsub = ES_VENC_QueryStatus(chnId, &status);
        if (ES_SUCCESS != retsub) {
            app_error("ES_VENC_QueryStatus chn[%d] failed with %#x! \n", chnId, retsub);
            // return retsub;
            break;
        }

        app_debug("chn:%d status.leftPics:%d %d exit:status %d \n", chnId, status.leftPics, status.leftStreamFrames,
                  pVencElement->sendFrameThdExit);
        if ((0 == status.leftPics) && (0 == status.leftStreamFrames)) {
            if (pVencElement->sendFrameThdExit) {
                app_debug("chn:%d exit!", chnId);
                // return ES_FAILURE;
                break;
            }
        }
        // pVencElement->getStreamQueryPerformance->performanceStaticEnd();

        /*******************************************************
        step 2.2 :suggest to check both curPacks and
        leftStreamFrames at the same time
        *******************************************************/
        if (0 == status.curPacks) {
            app_debug("%s \n", "NOTE: Current frame is NULL!");
            // return ES_SUCCESS;
            continue;
        }

        /*******************************************************
         step 2.3 : malloc corresponding number of pack nodes.
        *******************************************************/
        stream->pPack = (VENC_PACK_S *)malloc(sizeof(VENC_PACK_S) * status.curPacks);
        if (NULL == stream->pPack) {
            app_error("%s %d \n", "malloc stream pack failed chn: ", chnId);
            // return ES_FAILURE;
            return ES_NULL;
        }
        /*******************************************************
         step 2.4 : call mpi to get one-frame stream
        *******************************************************/
        stream->packCount = status.curPacks;
        retsub = ES_VENC_GetStream(chnId, stream, -1);  //-1 block
        if (ES_SUCCESS != retsub) {
            app_warn("ES_VENC_GetStream chn[%d] failed with %#x! \n", chnId, retsub);
            // return ES_SUCCESS;
            continue;
        }

        /*******************************************************
        step 2.5 : save stream to file
        *******************************************************/
        if (pChn->packDumpFlag) {
            //  CVideoPacketMeta : public CBaseMeta
            // string source;     // images source info,such as IPC ,mp4 file,
            // jpeg file int index;             // the index framers since
            // decoding start ulong pts;             // the framer pts,used for
            // the source of video. long long int srcTime; // the time when
            // CFrameMeta create in source PAYLOAD_TYPE_E type; ES_S32 width;
            // ES_S32 height;
            // VDEC_STREAM_S* videoPkt;
            CVideoPacketMetaVe *pVideoPacketMeta = new CVideoPacketMetaVe();
            pVideoPacketMeta->encVideoPkt = stream;
            pVideoPacketMeta->type = pVencElement->type;
            pVideoPacketMeta->chnId = pVencElement->chnId;
            app_ret rett = pVencElement->TransMitToNextToProcess((CBaseMeta *)pVideoPacketMeta);

            if (ES_SUCCESS != rett) {
                app_error("%s \n", "save stream failed!");
                break;
            }
        }

        pVencElement->getStreamPerformance->performanceStaticEnd();
    }

    /*******************************************************
    step 2.7 : free pack nodes
    *******************************************************/

    ret = ES_VENC_CloseFd(chnId);
    if (ES_SUCCESS != ret) {
        app_error("%s %d\n", "ES_VENC_CloseFd failed!", chnId);
    }

    // send package with eosflag
    CVideoPacketMetaVe *pVideoPacketMeta = new CVideoPacketMetaVe();
    pVideoPacketMeta->encVideoPkt = nullptr;
    pVideoPacketMeta->type = pVencElement->type;
    pVideoPacketMeta->chnId = pVencElement->chnId;
    pVideoPacketMeta->eosFlag = true;
    pVencElement->TransMitToNextToProcess((CBaseMeta *)pVideoPacketMeta);

    app_info("%s %s \n", pVencElement->mName.c_str(), "Exit thread getStreamProcByThread");
    return ES_NULL;
}

static ES_S32 startGetStreamByThread(const TEST_Client_S_ENC *pEncClient, ES_S32 chnId) {
    TEST_CHN_S_ENC *pChn = pEncClient->pMultiChn[chnId];
    pthread_create(&pChn->getData.getDataPid, 0, getStreamProcByThread, (ES_VOID *)pChn);
    app_info("%s %d\n", "venc getStream thread for chn created ", chnId);

    return ES_SUCCESS;
}

ES_S32 startGetStream(const TEST_Client_S_ENC *pEncClient, ES_S32 chnId) {
    app_debug("%s %d \n", "Enter chnId:", chnId);
    ES_U32 i = chnId;
    // TEST_CHN_S_ENC* pChn = pEncClient->pMultiChn[i];
    VENC_CHN_ATTR_S vencChnAttr;
    if (ES_SUCCESS != ES_VENC_GetChnAttr(i, &vencChnAttr)) {
        app_error("%s %d \n", "ES_VENC_GetChnAttr chn failed! ", i);
        ASSERT(0);
    }

    app_debug("%s %d\n", "Exit chnId:", chnId);
    return startGetStreamByThread(pEncClient, chnId);
}

app_ret VencElement::perfStat() {
    getStreamPerformance->performanceStaticReport();
    sendFramePerformance->performanceStaticReport();
    return APP_SUCCESS;
}

app_ret VencElement::Init() {
    /**
     * parse config.
     */
    ES_S32 ret = ES_SUCCESS;
    memset(&mEncClient, 0, sizeof(TEST_Client_S_ENC));

    ret = parseCfg((char *)m_configFile.c_str(), &mEncClient);

    if (ret != ES_SUCCESS) {
        app_error("%s %#x.\n", "command parse failed ", ret);
        return APP_FAILURE;
    }

    // merge to global gEncClient
    auto &sharedCounter = SharedCounter::sharedCounter();
    if (sharedCounter.lockSharedMemory() == -1) {
        app_error("VENC Init get shared mem lock failed!\n");
        return APP_FAILURE;
    }

    try {
        chnId = sharedCounter.vencCounterGet();
        // chnId = gEncClient.groupNum++;
        gEncClient.groupNum++;
        TEST_CHN_S_ENC *chnInfo = mEncClient.pMultiChn[mEncClient.startGrpId];
        chnInfo->grpId = chnId;
        chnInfo->multiple = 1;
        chnInfo->element = (void *)this;
        type = chnInfo->type;
        gEncClient.pMultiChn[chnId] = chnInfo;
        gEncClient.startGrpId = sharedCounter.vencOffsetGet();
        frameDumpflag = chnInfo->frameDumpFlag;

        // performance statics
        sendFramePerformance = new PerformanceStatic(mName + "_sendFramePerformance", PERF_STATIC_SEGMENT);
        getStreamPerformance = new PerformanceStatic(mName + "_getStreamPerformance", PERF_STATIC_SEGMENT);

        sendFrameThdExit = ES_FALSE;
        sharedCounter.vencCounterIncrement();
    } catch (...) {
        sharedCounter.unlockSharedMemory();
        app_error("VENC enelemt try to init failed \n");
        throw;
    }
    sharedCounter.unlockSharedMemory();
    return APP_SUCCESS;
}

app_ret VencElement::Start() {
    /**
     * prepare and create encoding channels, then start receive.
     */

    const TEST_Client_S_ENC *pEncClient = &gEncClient;
    if (!gEncStarted) {
        app_info("%s %s\n", "Start Innter only print once ", mName.c_str());
        ES_BOOL bEncExisted = pEncClient->groupNum > 0 ? ES_TRUE : ES_FALSE;
        if (bEncExisted) {
            /* start venc. */
            ES_S32 ret = initAndStart(pEncClient);
            if (ES_SUCCESS != ret) {
                app_error("%s  %#x!\n", "Venc Start failed for ", ret);
            }
        }
        auto &sharedCounter = SharedCounter::sharedCounter();
        if (sharedCounter.lockSharedMemory() == -1) {
            app_error("VENC Start get shared mem lock failed!\n");
            return APP_FAILURE;
        }

        try {
            sharedCounter.vencOffsetIncrement(pEncClient->groupNum);
        } catch (...) {
            sharedCounter.unlockSharedMemory();
            app_error("VENC enelemts try to increase shared offset failed \n");
            throw;
        }
        sharedCounter.unlockSharedMemory();
        gEncStarted = true;
    }
    return APP_SUCCESS;
}

app_ret VencElement::Wait() {
    if (chnStarted) {
        TEST_Client_S_ENC *pEncClient = &gEncClient;
        stopGetStream(pEncClient, chnId);
        app_debug("%s \n", "Finish Innter only print once ");
        destroyChns(chnId);
        app_debug("%s \n", "befor deinitOptions ");
        deinitOptions(pEncClient, chnId);
        app_info("%s \n", "after deinitOptions ");
    }

    return APP_SUCCESS;
}

// CBaseMeta *baseMeta对当前接口而言，接收的应该是batchmeta
app_ret VencElement::ProcessData(CBaseMeta *baseMeta, CElement const *previousElement) {
    ES_S32 ret = ES_SUCCESS;
    app_debug("enter name:%s chnId:%d  \n", mName.c_str(), chnId);

    CFrameMeta *frameMeta = nullptr;
    CBatchMeta *batchMeta = nullptr;

    // assert(baseMeta->mMetaType!=nullptr);
    bool eosFlag = false;
    if (FRAME_META == baseMeta->mMetaType) {
        frameMeta = (CFrameMeta *)baseMeta;
        eosFlag = frameMeta->eosFlag;
    } else {
        batchMeta = (CBatchMeta *)baseMeta;
        eosFlag = batchMeta->eosFlag;
    }

    // end(last) frame
    if (eosFlag) {
        app_debug("ES_VENC_StopRecvPic vechn[%d] end with %#x! \n", chnId, ret);
        ret = ES_VENC_StopRecvFrame(chnId);
        sendFrameThdExit = ES_TRUE;
        if (frameMeta) {
            frameMeta->reduceUseCount();
        } else {
            batchMeta->reduceUseCount();
        }

        app_info("SendFrame thread exit: channel[%d] \n", chnId);

        return APP_SUCCESS;
    }

    // common frame
    int dataType = 9;  // 9:unknow,0:framemeta pp0,1:framemeta pp1,2:batch meta(grid)
    VIDEO_FRAME_INFO_S *videoFrameInfo = nullptr;
    if (frameMeta) {
        CImage *cimage = frameMeta->images[0];  // pp0
        dataType = 0;
        if (frameMeta->images.size() > 1) {  // exist pp1 ,use pp1
            cimage = frameMeta->images[1];
            dataType = 1;
        }
        videoFrameInfo = cimage->mPic;
    } else {
        assert(batchMeta->videoGrid->width > 0);
        videoFrameInfo = batchMeta->videoGrid->gridPic;
        assert(videoFrameInfo != nullptr);
        dataType = 2;
    }
    app_debug("sendframe datatype %d  \n", dataType);

    VIDEO_FRAME_S videoFrame = videoFrameInfo->videoFrame;
    if (!chnStarted) {
        const TEST_Client_S_ENC *pEncClient = &gEncClient;
        /* Create encoding get stream threads. */

        TEST_CHN_S_ENC *pChnInfo = pEncClient->pMultiChn[chnId];
        pChnInfo->width = videoFrame.width;
        pChnInfo->height = videoFrame.height;
        pChnInfo->cropParam.rect.width = videoFrame.width;
        pChnInfo->cropParam.rect.height = videoFrame.height;
        app_debug("sendframe chnStarted %s %d %d \n", mName.c_str(), videoFrame.width, videoFrame.height);

        startChn(pEncClient, chnId);
        startGetStream(pEncClient, chnId);
        chnStarted = true;
        app_info("startChn startGetStream name:%s chnId:%d  \n", mName.c_str(), chnId);
    }

    if (frameDumpflag) {
        if (frameMeta) {
            app_debug("sendframe 0x%p   write framemeta:%d\n", &frameMeta->images, frameCount);
        } else {
            app_debug("sendframe 0x%p   write batchmeta:%d\n", &batchMeta->videoGrid, frameCount);
        }

        string fileName = "sendframe_" + mName + "_" + to_string(frameCount) + ".yuv";

        ES_U64 *pVirAddr = ES_NULL;
        ES_U32 size = videoFrame.width * videoFrame.height + videoFrame.width * videoFrame.height / 2;
        pVirAddr = (ES_U64 *)ES_SYS_Mmap(videoFrame.fd, size, SYS_CACHE_MODE_NOCACHE);

        if (NULL != pVirAddr) {
            dumpFile(fileName, (char *)pVirAddr, size);
            ret = ES_SYS_Munmap(pVirAddr, size);
            if (ret > 0) {
                app_error("frameDumpflag name:%s chnId:%d  %d unmap error\n", mName.c_str(), chnId, ret);
            }
        } else {
            app_error("frameDumpflag name:%s chnId:%d  pVirAddr is null\n", mName.c_str(), chnId);
        }
    }

    sendFramePerformance->performanceStaticStart();
    ret = ES_VENC_SendFrame(chnId, videoFrameInfo, -1);
    sendFramePerformance->performanceStaticEnd();
    if (ES_SUCCESS != ret) {
        app_error("ES_VENC_SendFrame failed, chn:%d, err:0x%x! \n", chnId, ret);
        return APP_FAILURE;
    }

    if (frameMeta) {
        app_debug("release name:%s chnId:%d  framemeta 0x%p \n", mName.c_str(), chnId, &frameMeta->images);
        frameMeta->reduceUseCount();
    } else {
        app_debug("release name:%s chnId:%d  batchmeta 0x%p \n", mName.c_str(), chnId, &batchMeta->videoGrid);
        batchMeta->reduceUseCount();
    }
    app_debug("exit name:%s chnId:%d  \n", mName.c_str(), chnId);
    frameCount++;
    return APP_SUCCESS;
}

app_ret VencElement::Finish() {
    if (!gEncFinished) {
        app_debug("%s \n", "befor ES_VENC_Deinit ");
        ES_VENC_Deinit();
        gEncFinished = true;
        app_info("%s \n", "after ES_VENC_Deinit ");
    }

    auto &sharedCounter = SharedCounter::sharedCounter();
    if (sharedCounter.lockSharedMemory() == -1) {
        app_error("VENC Finish get shared mem lock failed!\n");
        return APP_FAILURE;
    }

    try {
        sharedCounter.vencCounterDecrement();
        sharedCounter.vencOffsetDecrement(1);
    } catch (...) {
        sharedCounter.unlockSharedMemory();
        app_error("VENC enelemts Finish try to decrease shared offset failed \n");
        throw;
    }
    sharedCounter.unlockSharedMemory();

    delete sendFramePerformance;
    delete getStreamPerformance;

    return APP_SUCCESS;
}

app_ret VencElement::ProcessAndTransmit(CBaseMeta *baseMeta, CElement const *self) {
    // wait for start function finished
    if (!mRunningFlag) {
        sem_wait(&mStartFlag);
        mRunningFlag = true;
    }

    ProcessData(baseMeta, this);
    return APP_SUCCESS;
}

extern "C" CElement *createEsVencElement(const char *name, const char *path) { return new VencElement(name, path); }