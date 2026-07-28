#ifndef PREPROC_YAML_PARSER_H_
#define PREPROC_YAML_PARSER_H_
#include "../common/commyamlparser.h"
#include "es_comm_vps.h"

struct MaintainAspectRatio {
    bool enable;  // keep src ration
    bool keep_long_side;

    bool keepDstRatio;
    int padding_value[3];
};

struct NextInferParam {
    bool enable;
    bool crop_enable;
    float scoreThreshold;
    float extendWidth;
    float extendHeight;
    int filterWidth;
    int filterHeight;
    //  vector<int> select_class_ids;
};
typedef struct {
    int die_id;
    /** Target unique ids */
    vector<int> target_infer_ids;
    /** Target unique ids */
    vector<int> select_class_ids;
    /** interval*/
    int interval[2];
    CDataFormat data_order;
    int out_pool_size;
    CDims dims;
    CDataType data_type;
    PIXEL_FORMAT_E pixel_format;
    int size_per_batch;
    MaintainAspectRatio aspectRatioParam;
    VPS_NORMALIZATION_INFO_S normalizationInfo;
    bool crop_enabled;
    int poolsize;
    int channelId;
    bool dumpFlag;
    NextInferParam nextInferParam;
} PREPROC_PARAM_S;

bool parse_config_file(PREPROC_PARAM_S *preProcessParam, string cfgFilePath);

#endif
