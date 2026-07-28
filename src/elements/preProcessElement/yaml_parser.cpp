#define PL_LOG_ID PL_LOG_PREPROC
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

#include "preProcElement.h"

bool parse_config_file(PREPROC_PARAM_S *preProcessParam, string cfgFilePath) {
    YAML::Node config = YAML::LoadFile(cfgFilePath);
    /*die-id*/
    preProcessParam->die_id = GET_KEY_VALUE(config, "DIE-id", 0);
    /*target-infer-ids*/
    YAML::Node tmp = GET_KEY_VALUE(config, "target-infer-ids", config["target-infer-ids"]);
    for (int i = 0; i < tmp.size(); i++) {
        int vulue = GET_LIST_VALUE(config, "target-infer-ids", i, -1);
        preProcessParam->target_infer_ids.push_back(vulue);
    }
    /*target_class_ids*/
    tmp = GET_KEY_VALUE(config, "select-class-ids", config["select-class-ids"]);
    for (int i = 0; i < tmp.size(); i++) {
        int vulue = GET_LIST_VALUE(config, "select-class-ids", i, -1);
        preProcessParam->select_class_ids.push_back(vulue);
    }
    /*interval*/
    // preProcessParam->interval = GET_KEY_VALUE( config, "interval", 0);
    preProcessParam->interval[0] = GET_LIST_VALUE(config, "interval", 0, 1);
    preProcessParam->interval[1] = GET_LIST_VALUE(config, "interval", 1, 1);
    /*outbuf pool size*/
    preProcessParam->out_pool_size = GET_KEY_VALUE(config, "poolsize", 10);
    /*channelId*/
    preProcessParam->channelId = GET_KEY_VALUE(config, "channel", 0);
    /*output-order*/
    /*0=UNKNOWN, 1=NCHW, 2=NHWC*/
    preProcessParam->data_order = (CDataFormat)GET_KEY_VALUE(config, "output-order", 0, true);

    /*output-format*/
    /*0=RGB, 1=RGBA, 2=ARGB, 3=BGR, 4=BGRA, 5=ABGR, 6=GRAY*/
    int format = GET_KEY_VALUE(config, "output-format", 0, true);
    /*output shape, for example: */
    /*batch_size: 8(N=8), output-width: 224(W=224), output-height: 224(H=224),
     * output-format: RGB(C=3)*/
    if (preProcessParam->data_order == NCHW) {
        preProcessParam->dims.n = GET_LIST_VALUE(config, "output-shape", 0, 8, true);
        preProcessParam->dims.c = GET_LIST_VALUE(config, "output-shape", 1, 3, true);
        preProcessParam->dims.h = GET_LIST_VALUE(config, "output-shape", 2, 224, true);
        preProcessParam->dims.w = GET_LIST_VALUE(config, "output-shape", 3, 224, true);
    } else if (preProcessParam->data_order == NHWC) {
        preProcessParam->dims.n = GET_LIST_VALUE(config, "output-shape", 0, 8, true);
        preProcessParam->dims.h = GET_LIST_VALUE(config, "output-shape", 1, 224, true);
        preProcessParam->dims.w = GET_LIST_VALUE(config, "output-shape", 2, 224, true);
        preProcessParam->dims.c = GET_LIST_VALUE(config, "output-shape", 3, 3, true);
    } else {
        return false;
    }

    /*data type*/
    /*#0=UINT8, 1=UINT16, 2=UINT32, 3=UINT64, 4=INT8, 5=INT16, 6=INT32, 7=INT64,
     * 8=FP16, 9=FP32, 10=FP64*/
    preProcessParam->data_type = (CDataType)GET_KEY_VALUE(config, "data-type", 4, true);

    /*output format*/
    preProcessParam->pixel_format =
        get_base_pixel_format(format, preProcessParam->data_type, preProcessParam->data_order);
    if (preProcessParam->pixel_format == PIXEL_FORMAT_UNKNOWN) {
        return false;
    }

    // poolsize
    preProcessParam->poolsize = GET_KEY_VALUE(config, "poolsize", 10);

    /*calculate the size of output batch data*/
    int Bpp;
    if (!esquerybpp(preProcessParam->pixel_format, &Bpp)) {
        return false;
    }

    preProcessParam->size_per_batch = preProcessParam->dims.n * preProcessParam->dims.w * preProcessParam->dims.h * Bpp;

    /*maintain_aspect_ratio*/
    tmp = GET_KEY_VALUE(config, "maintain_aspect_ratio", config["maintain_aspect_ratio"]);
    preProcessParam->aspectRatioParam.enable = (bool)GET_KEY_VALUE<bool>(tmp, "enable", false);
    {
        for (int i = 0; i < 3; i++) {
            preProcessParam->aspectRatioParam.padding_value[i] = GET_LIST_VALUE(tmp, "padding-value", i, 0, false);
        }
        preProcessParam->aspectRatioParam.keep_long_side = (bool)GET_KEY_VALUE<bool>(tmp, "keep-long-side", false);
        preProcessParam->aspectRatioParam.keepDstRatio = (bool)GET_KEY_VALUE<bool>(tmp, "keep-dst-ratio", false);
    }

    /*normalize*/
    tmp = GET_KEY_VALUE(config, "normalize", config["normalize"]);
    preProcessParam->normalizationInfo.bEnable = (ES_BOOL)GET_KEY_VALUE<bool>(tmp, "enable", false);
    {
        preProcessParam->normalizationInfo.param.normalizationMode =
            (VPS_NORMALIZATION_MODE_E)GET_KEY_VALUE(tmp, "normalizationmode", 0, true);
        float tmp1 = 0;
        if (preProcessParam->normalizationInfo.param.normalizationMode == VPS_NORMALIZATION_MIN_MAX) {
            VPS_NORMALIZATION_PARAMS_S *param = &preProcessParam->normalizationInfo.param;
            tmp1 = GET_LIST_VALUE<float>(tmp, "maxminreciprocal", 0, 0, true);
            memcpy(&param->maxMinReciprocal.r, &tmp1, sizeof(float));
            tmp1 = GET_LIST_VALUE<float>(tmp, "maxminreciprocal", 1, 0, true);
            memcpy(&param->maxMinReciprocal.g, &tmp1, sizeof(float));
            tmp1 = GET_LIST_VALUE<float>(tmp, "maxminreciprocal", 2, 0, true);
            memcpy(&param->maxMinReciprocal.b, &tmp1, sizeof(float));
            tmp1 = GET_LIST_VALUE<float>(tmp, "minvalue", 0, 0, true);
            memcpy(&param->minValue.r, &tmp1, sizeof(float));
            tmp1 = GET_LIST_VALUE<float>(tmp, "minvalue", 1, 0, true);
            memcpy(&param->minValue.g, &tmp1, sizeof(float));
            tmp1 = GET_LIST_VALUE<float>(tmp, "minvalue", 2, 0, true);
            memcpy(&param->minValue.b, &tmp1, sizeof(float));
        } else if (preProcessParam->normalizationInfo.param.normalizationMode == VPS_NORMALIZATION_Z_SCORE) {
            VPS_NORMALIZATION_PARAMS_S *param = &preProcessParam->normalizationInfo.param;
            // need apply with  input format?
            int scale = 1;
            if (preProcessParam->normalizationInfo.bEnable) {
                scale = 255;
            } else {
                scale = 1;
            }

            tmp1 = GET_LIST_VALUE<float>(tmp, "stdreciprocal", 0, 1, true);
            tmp1 = 1.0 / (tmp1 * scale);
            param->stdReciprocal.r = *((ES_U32 *)&tmp1);

            tmp1 = GET_LIST_VALUE<float>(tmp, "stdreciprocal", 1, 1, true);
            tmp1 = 1.0 / (tmp1 * scale);
            param->stdReciprocal.g = *((ES_U32 *)&tmp1);

            tmp1 = GET_LIST_VALUE<float>(tmp, "stdreciprocal", 2, 1, true);
            tmp1 = 1.0 / (tmp1 * scale);
            param->stdReciprocal.b = *((ES_U32 *)&tmp1);

            tmp1 = GET_LIST_VALUE<float>(tmp, "meanvalue", 0, 0, true);
            tmp1 = (tmp1 * scale);
            param->meanValue.r = *((ES_U32 *)&tmp1);

            tmp1 = GET_LIST_VALUE<float>(tmp, "meanvalue", 1, 0, true);
            tmp1 = (tmp1 * scale);
            param->meanValue.g = *((ES_U32 *)&tmp1);

            tmp1 = GET_LIST_VALUE<float>(tmp, "meanvalue", 2, 0, true);
            tmp1 = (tmp1 * scale);
            param->meanValue.b = *((ES_U32 *)&tmp1);
        }
        tmp1 = GET_KEY_VALUE<float>(tmp, "stepreciprocal", 0, false);
        memcpy(&preProcessParam->normalizationInfo.param.stepReciprocal, &tmp1, sizeof(float));
    }

    /*crop*/
    preProcessParam->crop_enabled = GET_KEY_VALUE<bool>(config["crop"], "enable", false);
    /*next-infer*/
    tmp = GET_KEY_VALUE(config, "next-infer", config["next-infer"]);
    preProcessParam->nextInferParam.enable = (ES_BOOL)GET_KEY_VALUE<bool>(tmp, "enable", false);
    {
        preProcessParam->nextInferParam.crop_enable = (ES_BOOL)GET_KEY_VALUE<bool>(tmp, "crop-enable", false);
        preProcessParam->nextInferParam.scoreThreshold = (float)GET_KEY_VALUE<float>(tmp, "scoreThreshold", 0.3, false);
#if 0
        tmp = GET_KEY_VALUE(config, "select-class-ids", config["select-class-ids"]);
        for (int i = 0; i < tmp.size(); i++) {
            int vulue = GET_LIST_VALUE(config, "select-class-ids", i, -1);
            preProcessParam->nextInferParam.select_class_ids.push_back(vulue);
        }
#endif
        preProcessParam->nextInferParam.extendWidth = (float)GET_KEY_VALUE<float>(tmp, "extendWidth", 0.1, false);
        preProcessParam->nextInferParam.extendHeight = (float)GET_KEY_VALUE<float>(tmp, "extendHeight", 0.1, false);
        preProcessParam->nextInferParam.filterWidth = (int)GET_KEY_VALUE<int>(tmp, "filterWidth", 2, false);
        preProcessParam->nextInferParam.filterHeight = (int)GET_KEY_VALUE<int>(tmp, "filterHeight", 2, false);

        app_debug(" preProcessParam->nextInferParam crop %d score %f extend [%f %f filter [ %d %d ]] ",
                  preProcessParam->nextInferParam.crop_enable, preProcessParam->nextInferParam.scoreThreshold,
                  preProcessParam->nextInferParam.extendWidth, preProcessParam->nextInferParam.extendHeight,
                  preProcessParam->nextInferParam.filterWidth, preProcessParam->nextInferParam.filterHeight);
    }

    /*dump*/
    preProcessParam->dumpFlag = GET_KEY_VALUE<bool>(config["dump"], "enable", false);

    return true;
}
