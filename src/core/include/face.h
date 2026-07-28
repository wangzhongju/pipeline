#ifndef _ESSDK_PIPELINE_MEDIA_META_H_
#define _ESSDK_PIPELINE_MEDIA_META_H_

#include "base_meta.h"

struct PersonInfo {
    int id;       // id in table
    string name;  // name
    string pic;   // picture path
    int sex;      // 0female,1male
    string jobNumber;
    std::vector<float> features;
};

class CFaceSelectMeta {
   public:
    CFaceSelectMeta() : faceFeatureFd(0), faceFeatureSize(0) {}
    ~CFaceSelectMeta() { release(); }

    void release() {
        // todo
        if (0 != bgrFaceFrame.fd) {
            ES_SYS_MemFree(bgrFaceFrame.fd);
            bgrFaceFrame.fd = 0;
        }
        if (0 != nv12FaceFrame.fd) {
            ES_SYS_MemFree(nv12FaceFrame.fd);
            nv12FaceFrame.fd = 0;
        }
        if (0 != faceFeatureFd && 0 != faceFeatureSize) {
            PL_ES_VB_ReleaseBlock(faceFeatureFd);
            faceFeatureFd = 0;
            faceFeatureSize = 0;
        }
    }

   public:
    VIDEO_FRAME_S bgrFaceFrame;   // 抠图的结果rgb
    VIDEO_FRAME_S nv12FaceFrame;  // 抠图的结果nv12

    ES_U64 faceFeatureFd;            // 推理输出的face feature FD
    ES_U64 faceFeatureSize;          //  face feature size
    ModelInfo faceFeatureModelInfo;  // face feature shape
    CObjectMeta *parentObjMeta;
    PersonInfo personInfo;                  // compare比对得到的个人信息
    CAudioFrameMeta *personInfoAudioFrame;  // 个人信息播报内容
};

#endif