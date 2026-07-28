#ifndef _ESSDK_PIPELINE_DATA_DEFINES_H_
#define _ESSDK_PIPELINE_DATA_DEFINES_H_

enum CDataType {
    DATA_U8 = 0,
    DATA_U16,
    DATA_U32,
    DATA_U64,
    DATA_S8,
    DATA_S16,
    DATA_S32,
    DATA_S64,
    DATA_F16,
    DATA_F32,
    DATA_F64,
    DATA_UNKNOW
};

enum CDataFormat {
    NCHW = 0,
    NHWC,
    NCXHWX,
};

enum BASE_QUERY_TYPE {
    VIDEO_STREAM_INFO = 0,
    AUDIO_STREAM_INFO,
};

#endif