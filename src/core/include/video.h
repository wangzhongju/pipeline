#ifndef _ESSDK_PIPELINE_VIDEO_H_
#define _ESSDK_PIPELINE_VIDEO_H_

#include <semaphore.h>

#include "base_meta.h"
#include "es_comm_vdec.h"
#include "es_comm_venc.h"
#include "es_comm_video.h"
#include "object_meta.h"
#include "pl_mem_wrap.h"
#include "pool.h"
#include "sw_performance.h"

using namespace std;

#define MAX_VIDEO_GRP_NUM 128
#define IPC_MAX_DELAY_FRAME_NUM 15

struct VideoStreamInfo {
    PAYLOAD_TYPE_E type;
    ES_S32 width;
    ES_S32 height;
};

struct IpcDecodeCnt {
    int ipcCnt;
    int totalVideoGrpCnt;
    int ipcIndexs[32];
    unsigned long long ipcToDecodingCount[32];
    unsigned long long ipcDecodedCount[32];
    int eosflag[32];
    int endframeflag[32];
    int endframecnt;
};

extern IpcDecodeCnt gIpcDecCnt;
extern uint64_t gVoCount;

class CVideoGridMeta {
   public:
    CVideoGridMeta() {};
    virtual ~CVideoGridMeta() {};

    virtual void release() {
        if (NULL != gridPic) {
            delete gridPic;
            gridPic = NULL;
        }

        originFrameMetas.clear();

        if (pool) {
            pool->deallocate(this);
        } else {
            delete this;
        }
    }
    std::vector<CFrameMeta *> originFrameMetas;
    ES_U64 memFd = 0;
    PIXEL_FORMAT_E data_format = PIXEL_FORMAT_BUTT;
    int data_size = 0;
    int width = 0;
    int height = 0;
    int stride[3] = {0};
    int rows = 0;
    int cols = 0;
    VIDEO_FRAME_INFO_S *gridPic = nullptr;
    MetaPool<CVideoGridMeta> *pool = nullptr;
};

class CImage {
   public:
    CImage() {}
    CImage(VIDEO_FRAME_INFO_S *pic) : mPic(pic), pool(nullptr) {}
    virtual ~CImage() {
        // release();
    };
    /*
      this api should be recovered by video decoder derived class,
      where dicpture would be found with the same es_dma_buf and then reuse it.
    */
    virtual void release() {
        if (mPic) {
            PL_ES_VB_ReleaseBlock(mPic->videoFrame.fd);
            free(mPic);
            mPic = nullptr;
        }
        if (pool) {
            pool->deallocate(this);
        } else {
            delete this;
        }
    }

   public:
    VIDEO_FRAME_INFO_S *mPic = nullptr;
    MetaPool<CImage> *pool = nullptr;
};

class CVideoPacketMeta : public CBaseMeta {
   public:
    CVideoPacketMeta() : CBaseMeta(VIDEO_PACKET_META) {}
    virtual void release() {
        if (videoPkt != nullptr) {
            free(videoPkt);
            videoPkt = nullptr;
        }

        // printf("-------pool:%p----------\n", pool);
        if (pool) {
            addUseCount();
            pool->deallocate(this);
        } else {
            delete this;
        }
    }

   public:
    string source;              // images source info,such as IPC ,mp4 file, jpeg file
    int index = 0;              // the index framers since decoding start
    ulong pts = 0;              // the framer pts,used for the source of video.
    long long int srcTime = 0;  // the time when CFrameMeta create in source
    PAYLOAD_TYPE_E type = PT_BUTT;
    ES_S32 width = 0;
    ES_S32 height = 0;
    VDEC_STREAM_S *videoPkt = nullptr;
    VENC_STREAM_S *encVideoPkt = nullptr;
    int chnId = 0;  // release need
    int isIpc = 0;
    int padIndex = 0;
    MetaPool<CVideoPacketMeta> *pool = nullptr;

   private:
    CVideoPacketMeta(const CVideoPacketMeta &) = delete;
    CVideoPacketMeta &operator=(const CVideoPacketMeta &) = delete;
};

class CFrameMeta : public CBaseMeta {
   public:
    CFrameMeta() : CBaseMeta(FRAME_META) {}
    virtual void release() {
        rawImgMetaReleasePerformance->performanceStaticStart();
        if (!images.empty()) {  // 检查容器
            for (auto img : images) {
                if (img != nullptr) {
                    img->release();  // 要求 release() 是线程安全的
                    img = nullptr;
                }
            }
            images.clear();  // 可选：清空容器
        }
        rawImgMetaReleasePerformance->performanceStaticEnd();

        objMetaReleasePerformance->performanceStaticStart();
        for (auto obj : objs) {
            obj->release();
        }
        objs.clear();

        for (auto obj : rtmObjs) {
            obj->release();
        }
        rtmObjs.clear();
        objMetaReleasePerformance->performanceStaticEnd();

        if (pool) {
            addUseCount();
            pool->deallocate(this);
        } else {
            delete this;
        }
    }

   public:
    string source;          // images source info,such as IPC ,mp4 file, jpeg file
    uint indexInBatch = 0;  // the index in batches data, in the case of batchimages
    uint padIndex;
    int index = 0;          // the index framers since decoding start
    ulong pts = 0;          // the framer pts,used for the source of video.
    long long int srcTime;  // the time when CFrameMeta create in source
    vector<float> feature;

    MetaPool<CFrameMeta> *pool = nullptr;

    vector<CImage *> images;        // vector size should be 2 for win2030 video decoder output,
                                    // index 0 for pp0(YUV),1 pp1(customed)
    vector<CObjectMeta *> objs;     // predict
    vector<CObjectMeta *> rtmObjs;  // predict
   private:
    CFrameMeta(const CFrameMeta &) = delete;
    CFrameMeta &operator=(const CFrameMeta &) = delete;
};

#endif