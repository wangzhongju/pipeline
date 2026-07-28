#ifndef _ESSDK_PIPELINE_OBJECT_META_H_
#define _ESSDK_PIPELINE_OBJECT_META_H_

#include <string>
#include <vector>

#include "base_meta.h"
#include "forward.h"
#include "pool.h"

#define MAX_OBJMETA_PER_FRAME 200

enum PL_OBJ_TYPE { PL_OBJ_YOLO, PL_OBJ_RTMPOSE, PL_OBJ_CLASSIFY, PL_OBJ_BUT = 100 };

struct CPoints {
    CPoints() {
        x = 0.0;
        y = 0.0;
    }
    float x;
    float y;
};

struct CBboxInfo {
    CBboxInfo() {
        left = -1.0;
        top = -1.0;
        width = -1.0;
        height = -1.0;
    };
    float left;
    float top;
    float width;
    float height;
};

struct CBoxINT {
    CBoxINT() {
        x = -1;
        y = -1;
        width = -1;
        height = -1;
    }
    int x;
    int y;
    int width;
    int height;
};

struct RtmPosePointInfo {
    RtmPosePointInfo() {
        x = 0;
        y = 0;
        score = -1.0;
    }
    int32_t x;
    int32_t y;
    float score;
};

class CObjectMeta {
   public:
    CObjectMeta() {}
    virtual ~CObjectMeta() {
        // release();
    }

    virtual void release() {
        parentFrameMeta = NULL;
        rtmPoseInfo.clear();
        keyPoint.clear();

        if (pool) {
            pool->deallocate(this);
        } else {
            delete this;  // malloc  when use tracker, so clear() need before this delete
        }
    }

    enum PL_OBJ_TYPE objType = PL_OBJ_BUT;
    int classId = -1;
    std::string objLable;

    CBboxInfo detectorBboxInfo;
    float detectorConfidence = 0.0;

    int trackerId = -1;
    CBboxInfo trackerBboxInfo;
    float trackerConfidence = 0.0;
    std::vector<RtmPosePointInfo> rtmPoseInfo;

    std::vector<CPoints> keyPoint;
    std::vector<CPoints> feature;

    CFrameMeta *parentFrameMeta = nullptr;

    MetaPool<CObjectMeta> *pool = nullptr;
};

#endif