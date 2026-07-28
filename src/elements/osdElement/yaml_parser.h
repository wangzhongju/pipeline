#ifndef OSD_YAML_PARSER_H_
#define OSD_YAML_PARSER_H_
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>

#include "../common/commyamlparser.h"

using namespace std;

typedef struct {
    float R;
    float G;
    float B;
} OSD_RGBF32_S;

struct rtmPoseOsdParam {
    bool enable;
    OSD_RGBF32_S rectcolor;
    int pensize;
    std::vector<int> body;
    std::vector<int> face;
    std::vector<int> left_hand;
    std::vector<int> right_hand;
    std::vector<int> left_foot;
    std::vector<int> right_foot;
    std::vector<std::vector<int>> keypointConnect;
};
typedef struct {
    int die_id;
    // texture
    bool text_enable;
    string font_file_path;
    OSD_RGBF32_S textcolor;
    float fontheight;
    // rect
    bool rect_enable;
    OSD_RGBF32_S rectcolor;
    int pensize;
    // time
    bool time_enable;
    // stable text
    bool stable_enable;
    string stable_text;
    float pos[2];
    /* channel id */
    int channelId;
    /*rtm pose OSD param */
    rtmPoseOsdParam rtmOsdParam;
    /*input frame data dumped*/
    bool input_dump_enable = false;
    /*output frame data dumped*/
    bool output_dump_enable = false;
} OSD_PARAM_S;

bool parse_config_file(OSD_PARAM_S *osdParam, string cfgFilePath);

#endif
