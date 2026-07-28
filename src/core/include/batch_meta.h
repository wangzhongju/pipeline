#ifndef _ESSDK_PIPELINE_BATCH_META_H_
#define _ESSDK_PIPELINE_BATCH_META_H_

#include "base_meta.h"
#include "infer.h"
#include "video.h"

class CBatchMeta : public CBaseMeta {
   public:
    CBatchMeta() : CBaseMeta(BATCH_META) {}

    CFrameMeta *getFrameMeta(int indexInBatch = 0, bool refFlag = false) {
        std::lock_guard<std::mutex> lock(mtx);
        if (mImgMetas.size() > indexInBatch) {
            CFrameMeta *frame = mImgMetas[indexInBatch];
            if (refFlag) {
                frame->addUseCount();
            }
            if (frame == NULL) {
                app_error("%s frame if null\n", __func__);
            }
            return frame;
        } else {
            app_error("%s indexInBatch(%d) is big than batchmeta size(%d)\n", __func__, indexInBatch, mImgMetas.size());
        }

        return NULL;
    }

    void addFrameMeta(CFrameMeta *fmeta) {
        std::lock_guard<std::mutex> lock(mtx);
        mImgMetas.push_back(fmeta);
    }

    void clearFrameMata() { releaseImgMetas(); }

    int getFrameMetaSize() { return mImgMetas.size(); }

    std::vector<CPreprocessMeta *> mBatchedImgs;  // batched images preprocessed for infer, later infer's reprocess may
                                                  // cover the last infer's in the same pipeline
    std::vector<CInferOutputMeta *> m_inferOutputMetaVec;
    CVideoGridMeta *videoGrid = nullptr;
    ulong creationTime = 0;  // the time of the batchMeta created
    int smuxTimeout = 40;    // control VO FPS  with smux
    uint64_t batchIndex = 0;

    MetaPool<CBatchMeta> *pool = nullptr;

   private:
    vector<CFrameMeta *> mImgMetas;  // raw images from deocder,
    std::mutex mtx;
    CBatchMeta(const CBatchMeta &) = delete;
    CBatchMeta &operator=(const CBatchMeta &) = delete;

   public:
    /*
    release() would release all the memory in mImgMetas and mBatchedImgs used
    before by default; if the image dma buffer in mImgMetas managed with pool,
    the virtual fuction should be derived; it is possible to reuse resource of
    CBatchMeta with empty vector, and es_dma_buff in mImgMetas for decoder;
    */
    void release() override  // release all the image related resource,
    {
        preMetaReleasePerformance->performanceStaticStart();
        releaseBatchedImgs();
        preMetaReleasePerformance->performanceStaticEnd();

        releaseImgMetas();

        npuMetaReleasePerformance->performanceStaticStart();
        releaseInferOutputMeta();
        npuMetaReleasePerformance->performanceStaticEnd();

        if (videoGrid) {
            videoGrid->release();
            videoGrid = NULL;
        }

        if (pool) {
            addUseCount();
            pool->deallocate(this);
        } else {
            delete this;
        }
    }

    /*
    releaseBatchedImgs() would release all the memory in its vector;
    */
    app_ret releaseBatchedImgs()  // reused by later infer in multi-infer pipeline
    {
        for (auto img : mBatchedImgs) {
            img->release();
        }
        mBatchedImgs.clear();
        return APP_SUCCESS;
    }

    /*
    releaseImgMetas() would release all the memory in its vector;
    */
    app_ret releaseImgMetas()  // reused by video/jpeg/camera decoder with pool resource
    {
        frameReleasePerformance->performanceStaticStart();
        for (auto meta : mImgMetas) {
            meta->reduceUseCount();
        }
        frameReleasePerformance->performanceStaticEnd();

        mImgMetas.clear();
        return APP_SUCCESS;
    }

    app_ret releaseInferOutputMeta() {
        for (auto outputMeta : m_inferOutputMetaVec) {
            outputMeta->release();
        }
        m_inferOutputMetaVec.clear();

        return APP_SUCCESS;
    }
};

#endif