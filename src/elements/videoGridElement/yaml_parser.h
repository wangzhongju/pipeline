#ifndef VIDEOGRID_YAML_PARSER_H_
#define VIDEOGRID_YAML_PARSER_H_
#include "../common/commyamlparser.h"
#include "es_comm_vps.h"

// struct MaintainAspectRatio
// {
//     bool enable;
//     int padding_value[4];
// };

typedef struct {
    int die_id;
    int poolsize;
    int sizePerFrame;
    int rows;
    int columns;
    int width;
    int height;
    int stride[3];
    PIXEL_FORMAT_E pixelFormat;
    int strideAlign;
    bool dumpFlag;
    string textPicPath;
    int textPicWidth;
    int textPicHeight;
    int color[3];
    int specialrectnum;
    int specialrow[4];
    int specialcol[4];
    /* channel ID*/
    int channelID;
} VIDEOGRID_PARAM_S;

bool parse_config_file(VIDEOGRID_PARAM_S *videoGridParam, string cfgFilePath);

#endif
