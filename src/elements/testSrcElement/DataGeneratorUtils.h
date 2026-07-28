#ifndef _DATA_GENERATOR_UTILS_H__
#define _DATA_GENERATOR_UTILS_H__

#include "element.h"
#include "es_sys.h"
#include "es_sys_memory.h"
#include "es_vb_memory.h"
#include "object_meta.h"
#include "batch_meta.h"

// Common data structures moved from testSrcElement.h
struct EncodeInputDataInfo {
    std::string filepath;
    int height;
    int width;
    VB_POOL pool;
    sem_t poolSem;
    int dateType;
};

struct VideoGridInputDataInfo {
    std::string filepath;
    int height;
    int width;
    VB_POOL pool;
    int dateType;
    sem_t gridSem;
};

// Forward declaration
class CBaseMeta;

// Helper function declaration
CBaseMeta *prepareEncodeInputData(EncodeInputDataInfo &encodeInputDataInfo, int fps, int frameIndex, FILE *fp);
VIDEO_FRAME_INFO_S *createVideoFrame(ES_U32 count, ES_U32 width, ES_U32 height, ES_U32 fps);

#endif // _DATA_GENERATOR_UTILS_H__
