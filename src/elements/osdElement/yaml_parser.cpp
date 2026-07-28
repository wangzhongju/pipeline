#define PL_LOG_ID PL_LOG_OSD
#include "yaml_parser.h"

#include "osdElement.h"

using namespace std;

// 获取关键点列表
std::vector<int> GET_KEYPOINT_LIST(YAML::Node config, std::string key) {
    std::vector<int> keypoints;
    if (config[key].IsDefined() && config[key].IsSequence()) {
        for (const auto& item : config[key]) {
            if (item.IsScalar()) {
                keypoints.push_back(item.as<int>());
            }
        }
    } else {
        DPRINT("warning: %s is not defined or invalid.\n", key.c_str());
    }
    return keypoints;
}

bool parse_config_file(OSD_PARAM_S* osdParam, string cfgFilePath) {
    YAML::Node config = YAML::LoadFile(cfgFilePath);
    YAML::Node tmp;
    /*DIE-id*/
    osdParam->die_id = GET_KEY_VALUE(config, "die-id", 0);
    /*out-buf-pool-size*/
    // osdParam->out_pool_size = GET_KEY_VALUE( config, "out-buf-pool-size",
    // 10);
    /*text*/
    tmp = GET_KEY_VALUE(config, "text", config["text"]);
    osdParam->text_enable = (bool)GET_KEY_VALUE<bool>(tmp, "enable", false);
    {
        osdParam->font_file_path = GET_KEY_VALUE<string>(tmp, "fontfile", "", true);

        int vulue = GET_LIST_VALUE(tmp, "pencolor", 0, 0);
        osdParam->textcolor.R = (float)vulue / 255;
        vulue = GET_LIST_VALUE(tmp, "pencolor", 1, 0);
        osdParam->textcolor.G = (float)vulue / 255;
        vulue = GET_LIST_VALUE(tmp, "pencolor", 2, 0);
        osdParam->textcolor.B = (float)vulue / 255;

        osdParam->fontheight = GET_KEY_VALUE<float>(tmp, "fontheight", 0.1, true);
    }

    /*rect*/
    tmp = GET_KEY_VALUE(config, "rect", config["rect"]);
    osdParam->rect_enable = (bool)GET_KEY_VALUE<bool>(tmp, "enable", false);
    {
        int vulue = GET_LIST_VALUE(tmp, "pencolor", 0, 0);
        osdParam->rectcolor.R = (float)vulue / 255;
        vulue = GET_LIST_VALUE(tmp, "pencolor", 1, 0);
        osdParam->rectcolor.G = (float)vulue / 255;
        vulue = GET_LIST_VALUE(tmp, "pencolor", 2, 0);
        osdParam->rectcolor.B = (float)vulue / 255;
        osdParam->pensize = GET_KEY_VALUE<int>(tmp, "pensize", 4, true);
    }

    tmp = GET_KEY_VALUE(config, "date", config["date"]);
    osdParam->time_enable = (bool)GET_KEY_VALUE<bool>(tmp, "enable", false);

    tmp = GET_KEY_VALUE(config, "stable", config["stable"]);
    osdParam->stable_enable = (bool)GET_KEY_VALUE<bool>(tmp, "enable", false);
    {
        osdParam->stable_text = GET_KEY_VALUE<string>(tmp, "stabletext", "", true);
        float vulue = GET_LIST_VALUE<float>(tmp, "stablepos", 0, 0.0);
        osdParam->pos[0] = (float)vulue;
        vulue = GET_LIST_VALUE<float>(tmp, "stablepos", 1, 0.0);
        osdParam->pos[1] = (float)vulue;
    }

    /*channelId*/
    osdParam->channelId = (int)GET_KEY_VALUE<int>(config, "channel", 1, false);

    tmp = GET_KEY_VALUE(config, "rtmpose", config["rtmpose"]);
    osdParam->rtmOsdParam.enable = (bool)GET_KEY_VALUE<bool>(tmp, "enable", false);
    {
        int vulue = GET_LIST_VALUE(tmp, "pencolor", 0, 0);
        osdParam->rtmOsdParam.rectcolor.R = (float)vulue / 255;
        vulue = GET_LIST_VALUE(tmp, "pencolor", 1, 0);
        osdParam->rtmOsdParam.rectcolor.G = (float)vulue / 255;
        vulue = GET_LIST_VALUE(tmp, "pencolor", 2, 0);
        osdParam->rtmOsdParam.rectcolor.B = (float)vulue / 255;
        osdParam->rtmOsdParam.pensize = GET_KEY_VALUE<int>(tmp, "pensize", 4, false);

        YAML::Node rtmpose = config["rtmpose"];

        YAML::Node keypointsNode = rtmpose["keypoints"];
        if (keypointsNode.IsDefined() && keypointsNode.IsMap()) {
            osdParam->rtmOsdParam.body = GET_KEYPOINT_LIST(keypointsNode, "body");
            osdParam->rtmOsdParam.face = GET_KEYPOINT_LIST(keypointsNode, "face");
            osdParam->rtmOsdParam.left_hand = GET_KEYPOINT_LIST(keypointsNode, "left_hand");
            osdParam->rtmOsdParam.right_hand = GET_KEYPOINT_LIST(keypointsNode, "right_hand");
            osdParam->rtmOsdParam.left_foot = GET_KEYPOINT_LIST(keypointsNode, "left_foot");
            osdParam->rtmOsdParam.right_foot = GET_KEYPOINT_LIST(keypointsNode, "right_foot");
            app_info(" body size %d\n", osdParam->rtmOsdParam.body.size());
        } else {
            app_error("warning: keypoints is not defined or invalid.\n");
        }
        // 设置 keypointConnect 字段
        YAML::Node keypointConnectNode = rtmpose["keypointConnect"];
        if (keypointConnectNode.IsDefined() && keypointConnectNode.IsSequence()) {
            for (const auto& pair : keypointConnectNode) {
                if (pair.IsSequence() && pair.size() == 2) {
                    osdParam->rtmOsdParam.keypointConnect.push_back({pair[0].as<int>(), pair[1].as<int>()});
                }
            }
        } else {
            app_error("warning: keypointConnect is not defined or invalid.\n");
        }
    }

    if (config["dump"].IsDefined()) {
        if (config["dump"]["inputenable"].IsDefined()) {
            osdParam->input_dump_enable = config["dump"]["inputenable"].template as<bool>();
        }

        if (config["dump"]["outputenable"].IsDefined()) {
            osdParam->output_dump_enable = config["dump"]["outputenable"].template as<bool>();
        }
    }

    return true;
}
