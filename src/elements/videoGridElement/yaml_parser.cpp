#define PL_LOG_ID PL_LOG_GRID
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <string>

#include "videoGridElement.h"

bool parse_config_file(VIDEOGRID_PARAM_S *videoGridParam, string cfgFilePath) {
    YAML::Node config = YAML::LoadFile(cfgFilePath);
    /*die-id*/
    videoGridParam->die_id = GET_KEY_VALUE(config, "die-id", 0);
    /**/
    videoGridParam->poolsize = GET_KEY_VALUE(config, "poolsize", 1);
    videoGridParam->rows = GET_KEY_VALUE(config, "rows", 1);
    videoGridParam->columns = GET_KEY_VALUE(config, "columns", 1);
    videoGridParam->width = GET_KEY_VALUE(config, "width", 1920);
    videoGridParam->height = GET_KEY_VALUE(config, "height", 1080);
    videoGridParam->strideAlign = GET_KEY_VALUE(config, "stride-align", 1);
    string out_format = GET_KEY_VALUE<string>(config, "output-format", "");

    videoGridParam->textPicHeight = GET_KEY_VALUE(config, "text-pic-height", 0);
    videoGridParam->textPicWidth = GET_KEY_VALUE(config, "text-pic-width", 0);
    videoGridParam->textPicPath = GET_KEY_VALUE<string>(config, "text-pic-path", "");

    assert(videoGridParam->textPicHeight > 0);
    assert(videoGridParam->textPicWidth > 0);
    assert(videoGridParam->textPicPath != "");

    memset(videoGridParam->stride, 0, 3 * sizeof(int));
    if (out_format == "nv12") {
        videoGridParam->pixelFormat = PIXEL_FORMAT_NV12;
        videoGridParam->stride[0] =
            ((videoGridParam->width - 1) / videoGridParam->strideAlign + 1) * videoGridParam->strideAlign;
        videoGridParam->stride[1] = videoGridParam->stride[0];
        videoGridParam->sizePerFrame =
            videoGridParam->stride[0] * videoGridParam->height + videoGridParam->stride[1] * videoGridParam->height / 2;
    } else if (out_format == "rgb") {
        videoGridParam->pixelFormat = PIXEL_FORMAT_R8G8B8;
        videoGridParam->stride[0] =
            ((videoGridParam->width * 3 - 1) / videoGridParam->strideAlign + 1) * videoGridParam->strideAlign;
        videoGridParam->stride[1] = videoGridParam->stride[0];
        videoGridParam->stride[2] = videoGridParam->stride[0];
        videoGridParam->sizePerFrame = videoGridParam->stride[0] * videoGridParam->height;
    } else if (out_format == "bgra") {
        videoGridParam->pixelFormat = PIXEL_FORMAT_B8G8R8A8;
        videoGridParam->stride[0] =
            ((videoGridParam->width * 4 - 1) / videoGridParam->strideAlign + 1) * videoGridParam->strideAlign;
        videoGridParam->stride[1] = videoGridParam->stride[0];
        videoGridParam->stride[2] = videoGridParam->stride[0];
        videoGridParam->sizePerFrame = videoGridParam->stride[0] * videoGridParam->height;
    }

    /*dump*/
    videoGridParam->dumpFlag = GET_KEY_VALUE<bool>(config, "dump", false);
    /**/
    if (config["specialrect"].IsDefined()) {
        videoGridParam->color[0] = GET_LIST_VALUE<int>(config["specialrect"], "color", 0, false);
        videoGridParam->color[1] = GET_LIST_VALUE<int>(config["specialrect"], "color", 1, false);
        videoGridParam->color[2] = GET_LIST_VALUE<int>(config["specialrect"], "color", 2, false);

        videoGridParam->specialrectnum = GET_KEY_VALUE<int>(config["specialrect"], "rectnum", false);
        for (int i = 0; i < videoGridParam->specialrectnum; i++) {
            videoGridParam->specialrow[i] = GET_LIST_VALUE<int>(config["specialrect"], "rowarray", i, false);
            videoGridParam->specialcol[i] = GET_LIST_VALUE<int>(config["specialrect"], "colarray", i, false);
        }
    } else {
        videoGridParam->specialrectnum = 0;
    }

    /* channel ID*/
    videoGridParam->channelID = (int)GET_KEY_VALUE<int>(config, "channel", 1, false);
    return true;
}
