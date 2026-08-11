#define PL_LOG_ID PL_LOG_POSTPROC
#include "postprocessElement.h"

#include <dirent.h>
#include <stdio.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>

#include "../common/commyamlparser.h"

std::vector<std::string> getFileList(std::string dir_name) {
    std::vector<std::string> result;
    const char *dir_name_c = dir_name.c_str();
    if (NULL == dir_name_c) {
        app_debug("%s-%d: %s\n", __func__, __LINE__, "dir_name is null !");
    }
    struct stat s;
    lstat(dir_name_c, &s);
    if (!S_ISDIR(s.st_mode)) {
        app_debug("%s-%d: %s\n", __func__, __LINE__, "dir_name is not a valid directory !");
        return result;
    }
    struct dirent *filename;
    DIR *dir;
    dir = opendir(dir_name_c);
    if (NULL == dir) {
        app_debug("%s-%d: %s %s\n", __func__, __LINE__, "Can not open dir : ", dir_name_c);
        return result;
    }
    while ((filename = readdir(dir)) != NULL) {
        std::string filePath = dir_name;
        if (strcmp(filename->d_name, ".") == 0 || strcmp(filename->d_name, "..") == 0) continue;
        filePath += filename->d_name;
        result.push_back(filePath);
    }

    std::sort(result.begin(), result.end(), [=](const std::string &a, const std::string &b) {
        std::string cmp_a{a.begin() + dir_name.size(), a.end() - 7};
        std::string cmp_b{b.begin() + dir_name.size(), b.end() - 7};
        return std::stoi(cmp_a) < std::stoi(cmp_b);
    });

    return result;
}

void getClassifyGoldenData(std::string filePath, std::vector<classifyGoldData> &_goldenData) {
    _goldenData.clear();
    std::vector<std::string> filePathlist = getFileList(filePath);

    for (int index = 0; index < filePathlist.size(); index++) {
        FILE *goldenDataFile = fopen(const_cast<char *>(filePathlist[index].c_str()), "r");
        if (NULL == goldenDataFile) {
            continue;
        }

        int ret = 0;
        int goldenID = 0;
        float goldenConfidence = 0.0;
        ret = fscanf(goldenDataFile, "%d", &goldenID);
        ret = fscanf(goldenDataFile, "%f", &goldenConfidence);
        classifyGoldData tempGold;
        tempGold.classID = goldenID;
        tempGold.confidence = goldenConfidence;
        _goldenData.push_back(tempGold);
    }
    return;
}

int PostprocessElement::parseLabelsFile(const std::string &labelsFilePath) {
    app_debug("%s-%d: %s\n", __func__, __LINE__, "start parseLabelsFile");
    std::ifstream labels_file(labelsFilePath);
    if (!labels_file.is_open()) {
        app_error("%s-%d: %s\n", __func__, __LINE__, "labels file open error");
        return -1;
    }
    m_postLabels.clear();
    size_t sequential_index = 0;
    std::string line;
    while (std::getline(labels_file, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        size_t class_id = sequential_index;
        std::string label = line;
        const size_t colon = line.find(':');
        if (colon != std::string::npos) {
            const std::string id_text = line.substr(0, colon);
            if (!id_text.empty() &&
                std::all_of(id_text.begin(), id_text.end(), [](unsigned char ch) {
                    return std::isdigit(ch) != 0;
                })) {
                class_id = std::stoull(id_text);
                label = line.substr(colon + 1);
            }
        }
        const size_t comma = label.find(',');
        if (comma != std::string::npos) {
            label.resize(comma);
        }
        while (!label.empty() && std::isspace(static_cast<unsigned char>(label.front()))) {
            label.erase(label.begin());
        }
        while (!label.empty() && std::isspace(static_cast<unsigned char>(label.back()))) {
            label.pop_back();
        }
        if (m_postLabels.size() <= class_id) {
            m_postLabels.resize(class_id + 1);
        }
        m_postLabels[class_id] = label;
        sequential_index = std::max(sequential_index + 1, class_id + 1);
    }

    if (labels_file.bad()) {
        app_error("%s-%d: %s\n", __func__, __LINE__, "labels file is bad");
        return -1;
    }
    app_debug("%s-%d: %s\n", __func__, __LINE__, "parseLabelsFile end");
    return 0;
}

bool ParseConfAttr(YAML::Node node, int class_index, PostProcessDetectionParams &params) {
    bool ret = true;

    // for (YAML::const_iterator itr = node.begin(); itr != node.end(); ++itr)
    // {
    //     std::string paramKey = itr->first.as<std::string>();
    //     if (paramKey == "detected-min-w")
    //     {
    //         params.detectionMinWidth = itr->second.as<int>();
    //         app_debug(" %s %d\n", "detector params detected-min-w is ",
    //         params.detectionMinWidth);
    //     }
    //     else if (paramKey == "detected-min-h")
    //     {
    //         params.detectionMinHeight = itr->second.as<int>();
    //         app_debug(" %s %d\n", "detector params detected-min-h is ",
    //         params.detectionMinHeight);
    //     }
    //     else if (paramKey == "detected-max-w")
    //     {
    //         params.detectionMaxWidth = itr->second.as<int>();
    //         app_debug(" %s %d\n", "detector params detected-max-w is ",
    //         params.detectionMaxWidth);
    //     }
    //     else if (paramKey == "detected-max-h")
    //     {
    //         params.detectionMaxHeight = itr->second.as<int>();
    //         app_debug(" %s %d\n", "detector params detected-max-h is ",
    //         params.detectionMaxHeight);
    //     }
    //     else if (paramKey == "minBoxes")
    //     {
    //         params.minBoxes = itr->second.as<int>();
    //         app_debug(" %s %d\n", "detector params minBoxes is ",
    //         params.minBoxes);
    //     }
    //     else if (paramKey == "pre-cluster-threshold")
    //     {
    //         params.preClusterThreshold = itr->second.as<float>();
    //         app_debug(" %s %.3f\n", "detector params pre-cluster-threshold is
    //         ", params.preClusterThreshold);
    //     }
    //     else if (paramKey == "post-cluster-threshold")
    //     {
    //         params.postClusterThreshold = itr->second.as<float>();
    //         app_debug(" %s %.3f\n", "detector params post-cluster-threshold
    //         is ", params.postClusterThreshold);
    //     }
    //     else if (paramKey == "eps")
    //     {
    //         params.eps = itr->second.as<float>();
    //         app_debug(" %s %.3f\n", "detector params eps is ", params.eps);
    //     }
    //     else if (paramKey == "group-threshold")
    //     {
    //         params.groupThreshold = itr->second.as<int>();
    //         app_debug(" %s %d\n", "detector params group-threshold is ",
    //         params.groupThreshold);
    //     }
    //     else if (paramKey == "dbscan-min-score")
    //     {
    //         params.minScore = itr->second.as<float>();
    //         app_debug(" %s %.3f\n", "detector params dbscan-min-score is ",
    //         params.minScore);
    //     }
    //     else if (paramKey == "nms-iou-threshold")
    //     {
    //         params.nmsIOUThreshold = itr->second.as<float>();
    //         app_debug(" %s %.3f\n", "detector params nms-iou-threshold is ",
    //         params.nmsIOUThreshold);
    //     }
    //     else if (paramKey == "topk")
    //     {
    //         params.topK = itr->second.as<int>();
    //         app_debug(" %s %d\n", "detector params topK is ", params.topK);
    //     }
    //     else if (paramKey == "roi-top-offset")
    //     {
    //         params.roiTopOffset = itr->second.as<int>();
    //         app_debug(" %s %d\n", "detector params roi-top-offset is ",
    //         params.roiTopOffset);
    //     }
    //     else if (paramKey == "roi-bottom-offset")
    //     {
    //         params.roiBottomOffset = itr->second.as<int>();
    //         app_debug(" %s %d\n", "detector params roi-bottom-offset is ",
    //         params.roiBottomOffset);
    //     }
    //     else
    //     {
    //         app_debug(" %s %s\n", "detector params Unknown parameter : ",
    //         paramKey.c_str());
    //     }
    // }

    return ret;
}

std::set<int> parseStringArray(std::string input) {
    std::stringstream longStr(input);
    std::string item;
    std::set<int> ret;
    while (std::getline(longStr, item, ';')) {
        ret.insert(stoi(item));
    }
    return ret;
}

app_ret parseDetectionOutParams(YAML::Node &configyml, DetectionOutParams &detectionParams) {
    if (configyml["detection-params"]) {
        std::set<std::string> mandatoryString{"network-name", "imgWH"};
        for (YAML::const_iterator itr = configyml["detection-params"].begin();
             itr != configyml["detection-params"].end(); ++itr) {
            std::string paramKey = itr->first.as<std::string>();
            if (paramKey == "network-name") {
                EsPostNetWorkName networkName = (EsPostNetWorkName)(itr->second.as<int>());
                app_debug(" %s %d\n", "detection-params network name is ", int(networkName));
                std::string netName;
                switch (networkName) {
                    case ESYOLOV3:
                        netName = "yolov3";
                        break;
                    case ESYOLOV4:
                        netName = "yolov4";
                        break;
                    case ESYOLOV5:
                        netName = "yolov5";
                        break;
                    case ESYOLOV7:
                        netName = "yolov7";
                        break;
                    case ESYOLOV8:
                        netName = "yolov8";
                        break;
                    case ESSSD:
                        netName = "ssd";
                        break;
                    case ESFRCNN:
                        netName = "frcnn";
                        break;
                    default:
                        app_error(" %s \n", "detection-params network name is not set ");
                        return APP_FAILURE;
                        break;
                }
                mandatoryString.erase(paramKey);
                detectionParams.detectionName = networkName;
                detectionParams.netName = netName;
                app_debug(" %s %s\n", "detection-params network name is ", detectionParams.netName.c_str());
            } else if (paramKey == "nms-method") {
                ESNmsMethod nmsMethod = (ESNmsMethod)(itr->second.as<int>());
                std::string nmsName;
                switch (nmsMethod) {
                    case ES_HARD_NMS:
                        nmsName = "ES_HARD_NMS";
                        break;
                    case ES_SOFT_NMS_GAUSSIAN:
                        nmsName = "ES_SOFT_NMS_GAUSSIAN";
                        break;
                    case ES_SOFT_NMS_LINEAR:
                        nmsName = "ES_SOFT_NMS_LINEAR";
                        break;
                    default:
                        nmsMethod = ES_HARD_NMS;
                        nmsName = "ES_HARD_NMS";
                        app_debug(" %s %s\n", "detection-params nmsMethod not set , will set : ", nmsName.c_str());
                        break;
                }
                detectionParams.nmsMethod = nmsMethod;
                app_debug(" %s %s\n", "detection-params nmsMethod is ", nmsName.c_str());
            } else if (paramKey == "iou-method") {
                ES_IouMethod iouMethod = (ES_IouMethod)(itr->second.as<int>());
                std::string iouName;
                switch (iouMethod) {
                    case ES_IOU:
                        iouName = "ES_IOU";
                        break;
                    case ES_GIOU:
                        iouName = "ES_GIOU";
                        break;
                    case ES_DIOU:
                        iouName = "ES_DIOU";
                        break;
                    default:
                        iouMethod = ES_IOU;
                        iouName = "ES_IOU";
                        app_debug(" %s %d\n", "detection-params iouMethod not set, will set ", iouName.c_str());
                        break;
                }
                detectionParams.iouMethod = iouMethod;
                app_debug(" %s %s\n", "detection-params iouMethod is ", iouName.c_str());
            } else if (paramKey == "imgWH") {
                int imgWHSize = configyml["detection-params"]["imgWH"].size();
                if (2 != imgWHSize) {
                    app_error(" %s \n",
                              "detection-params imgWH is not set success, "
                              "please check params ");
                    return APP_FAILURE;
                }
                mandatoryString.erase(paramKey);
                detectionParams.inputWH.W = configyml["detection-params"]["imgWH"][0].template as<int>();
                detectionParams.inputWH.H = configyml["detection-params"]["imgWH"][1].template as<int>();
                app_debug(" %s [%d, %d]\n", "detection-params imgWH [w, h] is ", detectionParams.inputWH.W,
                          detectionParams.inputWH.H);
            } else if (paramKey == "inputTensorNum") {
                int inputTensorNum = itr->second.as<int>();
                detectionParams.inputTensorNum = inputTensorNum;
                app_debug(" %s %d\n", "detection-params inputTensorNum is ", detectionParams.inputTensorNum);
            } else if (paramKey == "outputTensorNum") {
                int outputTensorNum = itr->second.as<int>();
                detectionParams.outputTensorNum = outputTensorNum;
                app_debug(" %s %d\n", "detection-params outputTensorNum is ", detectionParams.outputTensorNum);
            } else if (paramKey == "classNum") {
                int classNum = itr->second.as<int>();
                detectionParams.classNum = classNum;
                app_debug(" %s %d\n", "detection-params classNum is ", detectionParams.classNum);
            } else if (paramKey == "anchorNum") {
                int anchorNum = itr->second.as<int>();
                detectionParams.anchorNum = anchorNum;
                app_debug(" %s %d\n", "detection-params anchorNum is ", detectionParams.anchorNum);
            } else if (paramKey == "anchorScale") {
                detectionParams.anchorScale.clear();
                int iScaleSize = configyml["detection-params"]["anchorScale"].size();
                for (int iSize = 0; iSize < iScaleSize; iSize++) {
                    std::vector<int> tempScale;
                    int iNumSize = configyml["detection-params"]["anchorScale"][iSize].size();
                    for (int iNumIndex = 0; iNumIndex < iNumSize; iNumIndex++) {
                        int tempNum = configyml["detection-params"]["anchorScale"][iSize][iNumIndex].template as<int>();
                        app_debug(" %s %d\n", "detection-params anchorScale is : ", tempNum);
                        tempScale.push_back(tempNum);
                    }
                    detectionParams.anchorScale.insert(detectionParams.anchorScale.end(), tempScale.begin(),
                                                       tempScale.end());
                }
            } else if (paramKey == "input-scale") {
                detectionParams.inputScale.clear();
                int iInputScaleSize = configyml["detection-params"]["input-scale"].size();
                for (int idx = 0; idx < iInputScaleSize; idx++) {
                    double tempScale = configyml["detection-params"]["input-scale"][idx].template as<double>();
                    app_debug(" %s %f\n", "detection-params input scale is : ", tempScale);
                    detectionParams.inputScale.push_back(tempScale);
                }
            } else if (paramKey == "maxbboxperclass") {
                int maxBboxPerClass = itr->second.as<int>();
                if (maxBboxPerClass > MAX_BBOX_PER_CLASS) {
                    app_debug(" %s %d %s %d\n", "detection-params maxbboxperclass is : ", maxBboxPerClass,
                              ", greater the MAX BBOX PER CLASS :", MAX_BBOX_PER_CLASS);
                    maxBboxPerClass = MAX_BBOX_PER_CLASS;
                }
                detectionParams.maxBboxPerClass = maxBboxPerClass;
                app_debug(" %s %d \n", "detection-params maxbboxperclass is : ", detectionParams.maxBboxPerClass);
            } else if (paramKey == "maxbboxperimg") {
                int maxBboxPerImg = itr->second.as<int>();
                if (maxBboxPerImg > MAX_BBOX_PER_IMG) {
                    app_debug(" %s %d %s %d\n", "detection-params maxbboxperimg is : ", maxBboxPerImg,
                              ", greater the MAX BBOX PER CLASS :", MAX_BBOX_PER_IMG);
                    maxBboxPerImg = MAX_BBOX_PER_IMG;
                }
                detectionParams.maxBboxPerImg = maxBboxPerImg;
                app_debug(" %s %d \n", "detection-params maxbboxperimg is : ", detectionParams.maxBboxPerImg);
            } else if (paramKey == "scoreThreshold") {
                float tempThd = itr->second.as<float>();
                detectionParams.scoreThreshold = tempThd;
                app_debug(" %s %f \n", "detection-params scoreThreshold is : ", detectionParams.scoreThreshold);
            } else if (paramKey == "iouThreshold") {
                float tempThd = itr->second.as<float>();
                detectionParams.iouThreshold = tempThd;
                app_debug(" %s %f \n", "detection-params iouThreshold is : ", detectionParams.iouThreshold);
            } else if (paramKey == "softnmssigma") {
                float tempThd = itr->second.as<float>();
                detectionParams.softnmssigma = tempThd;
                app_debug(" %s %f \n", "detection-params softnmssigma is : ", detectionParams.softnmssigma);
            } else if (paramKey == "imgOffset") {
                int offSetSize = configyml["detection-params"]["imgOffset"].size();
                if (2 != offSetSize) {
                    app_error(" %s \n",
                              "detection-params imgWH is not set success, "
                              "please check params ");
                    continue;
                }
                detectionParams.imgOffset.offX = configyml["detection-params"]["imgOffset"][0].template as<int>();
                detectionParams.imgOffset.offY = configyml["detection-params"]["imgOffset"][1].template as<int>();
                app_debug(" %s [%d, %d]\n", "detection-params imgOffset [offX, offY] is ",
                          detectionParams.imgOffset.offX, detectionParams.imgOffset.offY);
            } else {
                app_debug(" %s %s\n", "detection-params Unknown parameter is ", paramKey.c_str());
            }
        }

        if (mandatoryString.size() > 0) {
            for (auto it = mandatoryString.begin(); it != mandatoryString.end(); it++) {
                std::string tempStr = *it;
                app_error(" %s %s\n", "The param is not set : ", tempStr.c_str());
            }
            return APP_FAILURE;
        }
    } else {
        app_error(" %s \n", "detection params is not set ");
        return APP_FAILURE;
    }
    return APP_SUCCESS;
}

app_ret PostprocessElement::parseConfigFile() {
    if (m_configFile.empty() || !std::filesystem::exists(m_configFile)) {
        app_error(" %s %s\n", "config file doesn't exist, the config file is : ", m_configFile.c_str());
        return APP_FAILURE;
    }

    YAML::Node configyml = YAML::LoadFile(m_configFile);

    if (!(configyml.size() > 0)) {
        app_error(" %s %s\n", "Unable to parse config file : ", m_configFile.c_str());
        return APP_FAILURE;
    }

    // Parse the config file here
    if (configyml["postprocess-params"]) {
        std::set<std::string> mandatoryString{"target-inferId", "network-type"};
        for (YAML::const_iterator itr = configyml["postprocess-params"].begin();
             itr != configyml["postprocess-params"].end(); ++itr) {
            std::string paramKey = itr->first.as<std::string>();
            if (paramKey == "target-inferId") {
                mandatoryString.erase(paramKey);
                int targetInferID = itr->second.as<int>();
                m_postprocessInitParams.m_targetInferID = targetInferID;
                app_debug(" %s %d\n",
                          "Postprocess-params target-inferId is : ", m_postprocessInitParams.m_targetInferID);
            } else if (paramKey == "network-type") {
                mandatoryString.erase(paramKey);
                switch (itr->second.as<int>()) {
                    case PostProcessNetworkType_Detector:
                    case PostProcessNetworkType_Classifier:
                    case PostProcessNetworkType_Segmentation:
                    case PostProcessNetworkType_InstanceSegmentation:
                    case PostProcessNetworkType_Rtmpose:
                        m_postprocessInitParams.networkType =
                            static_cast<PostProcessNetworkType>(itr->second.as<int>());
                        break;
                    default:
                        app_error(" %s %d\n", "Error. Invalid value for network-type : ", itr->second.as<int>());
                        return APP_FAILURE;
                        break;
                }
                app_debug(" %s %s\n", "Postprocess-params network-type is : ",
                          m_postprocessInitParams.networkType == 0   ? "detector"
                          : m_postprocessInitParams.networkType == 1 ? "classify"
                                                                     : "segment");
            } else if (paramKey == "die-id") {
                uint dieID = itr->second.as<uint>();
                if (dieID > 2) {
                    m_postprocessInitParams.dieID = 0;
                } else {
                    m_postprocessInitParams.dieID = dieID;
                }

                app_debug(" %s %d\n", "Postprocess-params die-id is : ", m_postprocessInitParams.dieID);
            } else if (paramKey == "dsp-id") {
                uint dspID = itr->second.as<uint>();
                if (0 == m_postprocessInitParams.dieID) {
                    if (dspID > 3 || dspID < 0) {
                        m_postprocessInitParams.dspID = 0;
                    } else {
                        m_postprocessInitParams.dspID = dspID;
                    }
                } else if (1 == m_postprocessInitParams.dieID) {
                    if (dspID > 7 || dspID < 4) {
                        m_postprocessInitParams.dspID = 4;
                    } else {
                        m_postprocessInitParams.dspID = dspID;
                    }
                }

                app_debug(" %s %d\n", "Postprocess-params dsp-id is : ", m_postprocessInitParams.dspID);
            } else if (paramKey == "softmaxScale") {
                m_postprocessInitParams.softmaxScale = itr->second.as<float>();
                app_debug(" %s %f\n", " the softmax scale is  : ", m_postprocessInitParams.softmaxScale);
            } else if (paramKey == "modelopmapfile") {
                m_postprocessInitParams.modelOpMapFile = itr->second.as<std::string>();
                app_debug(" %s %s\n", " the modelopmapfile  : ", m_postprocessInitParams.modelOpMapFile.c_str());
            } else if (paramKey == "optype") {
                m_postprocessInitParams.optype = itr->second.as<int>();
                app_debug(" %s %d\n", " the optype  : ", m_postprocessInitParams.optype);
            } else if (paramKey == "classifygoldenDataPath") {
                m_postprocessInitParams.goldDataPath = itr->second.as<std::string>();
                app_debug(" %s %s\n", " the goldDataPath  : ", m_postprocessInitParams.goldDataPath.c_str());
                getClassifyGoldenData(m_postprocessInitParams.goldDataPath, m_goldenData);
            } else {
                app_debug(" %s %s\n", "Unknown parameter : ", paramKey.c_str());
            }
        }
        if (mandatoryString.size() > 0) {
            for (auto it = mandatoryString.begin(); it != mandatoryString.end(); it++) {
                std::string tempStr = *it;
                app_error(" %s %s\n", "The param is not set : ", tempStr.c_str());
            }
            return APP_FAILURE;
        }
    } else {
        app_error(" %s \n", "Postprocess params is not set ");
        return APP_FAILURE;
    }

    if (m_postprocessInitParams.modelOpMapFile.size()) {
        ifstream ifs;
        ifs.open(m_postprocessInitParams.modelOpMapFile.c_str(), ios::in);
        if (!ifs.is_open()) {
            m_postprocessInitParams.modelHaveDspOp = false;
        } else {
            char buf[1024] = {0};
            while (ifs >> buf) {
                std::string result(buf);
                transform(result.begin(), result.end(), result.begin(), ::tolower);
                if ("softmax" == result) {
                    m_postprocessInitParams.modelHaveDspOp = true;
                    break;
                }
            }
            ifs.close();
        }
    } else {
        m_postprocessInitParams.modelHaveDspOp = false;
    }
    app_debug(" %s %d\n", "the model have dsp op flag:  ", (int)m_postprocessInitParams.modelHaveDspOp);

    if (PostProcessNetworkType_Detector == m_postprocessInitParams.networkType) {
        if (configyml["detector-params"]) {
            std::set<std::string> mandatoryString{"labelfile-path"};
            for (YAML::const_iterator itr = configyml["detector-params"].begin();
                 itr != configyml["detector-params"].end(); ++itr) {
                std::string paramKey = itr->first.as<std::string>();
                if (paramKey == "labelfile-path") {
                    mandatoryString.erase(paramKey);
                    m_postprocessInitParams.labelsFilePath = itr->second.as<std::string>();
                    app_debug(" %s %s\n", "Detector-params labelfile-path is ",
                              m_postprocessInitParams.labelsFilePath.c_str());
                } else if (paramKey == "cluster-mode") {
                    m_postprocessInitParams.clusterMode = static_cast<PostProcessClusterMode>(itr->second.as<int>());
                    app_debug(" %s %d\n", "Detector-params cluster-mode is ", int(m_postprocessInitParams.clusterMode));
                } else if (paramKey == "attach-class-ids") {
                    m_postprocessInitParams.attachClassIds = parseStringArray(itr->second.as<std::string>());
                    for (auto it = m_postprocessInitParams.attachClassIds.begin();
                         it != m_postprocessInitParams.attachClassIds.end(); it++) {
                        app_debug(" %s %d\n", "Detector-params attach-class-ids is ", *it);
                    }
                } else {
                    app_debug(" %s %s\n", "Detector-params Unknown parameter is ", paramKey.c_str());
                }
            }
            if (mandatoryString.size() > 0) {
                for (auto it = mandatoryString.begin(); it != mandatoryString.end(); it++) {
                    std::string tempStr = *it;
                    app_error(" %s %s\n", "Detector-params parameter is not set : ", tempStr.c_str());
                }
                return APP_FAILURE;
            }
        } else {
            app_error(" %s \n", "Detector params is not set ");
            return APP_FAILURE;
        }

        PostProcessDetectionParams detection_params;
        if (configyml["class-attrs-all"]) {
            bool ret = ParseConfAttr(configyml["class-attrs-all"], -1, detection_params);
            if (!ret) {
                app_error(" %s\n", "Parsing 'class-attrs-all' group failed ");
                return APP_FAILURE;
            }
        }

        m_postprocessInitParams.generalParams = detection_params;

        for (YAML::const_iterator itr = configyml.begin(); itr != configyml.end(); ++itr) {
            std::string paramKey = itr->first.as<std::string>();
            std::string class_str = "class-attrs-";
            if ((paramKey != "class-attrs-all") && (paramKey.size() >= class_str.size())) {
                if (class_str.compare(0, class_str.size(), paramKey.c_str(), class_str.size()) == 0) {
                    std::string num_str = paramKey.substr(class_str.size());
                    int class_index = stoi(num_str);

                    PostProcessDetectionParams specifial_params;
                    bool ret = ParseConfAttr(configyml[paramKey], class_index, specifial_params);
                    if (!ret) {
                        app_error(" %s %s\n", "Parsing group failed : ", paramKey.c_str());
                        return APP_FAILURE;
                    }
                    m_postprocessInitParams.specificParams[class_index] = specifial_params;
                }
            }
        }

        parseDetectionOutParams(configyml, m_postprocessInitParams.detectionParams);
    } else if (PostProcessNetworkType_Classifier == m_postprocessInitParams.networkType) {
        if (configyml["classifier-params"]) {
            std::set<std::string> mandatoryString{"classifier-threshold", "classifier-type", "labelfile-path"};
            for (YAML::const_iterator itr = configyml["classifier-params"].begin();
                 itr != configyml["classifier-params"].end(); ++itr) {
                std::string paramKey = itr->first.as<std::string>();
                if (paramKey == "classifier-threshold") {
                    mandatoryString.erase(paramKey);
                    m_postprocessInitParams.classifierThreshold = (float)itr->second.as<float>();
                    app_debug(" %s %0.3f\n", "Classifier params classifier-threshold is : ",
                              m_postprocessInitParams.classifierThreshold);
                } else if (paramKey == "classifier-type") {
                    mandatoryString.erase(paramKey);
                    m_postprocessInitParams.classifierType = itr->second.as<std::string>();
                    app_debug(" %s %s\n", "Classifier params classifier-type is : ",
                              m_postprocessInitParams.classifierType.c_str());
                } else if (paramKey == "labelfile-path") {
                    mandatoryString.erase(paramKey);
                    m_postprocessInitParams.labelsFilePath = itr->second.as<std::string>();
                    app_debug(" %s %s\n",
                              "Classifier params labelfile-path is : ", m_postprocessInitParams.labelsFilePath.c_str());
                } else {
                    app_debug(" %s %s\n", "Classifier params Unknown parameter is : ", paramKey.c_str());
                }
            }
            if (mandatoryString.size() > 0) {
                for (auto it = mandatoryString.begin(); it != mandatoryString.end(); it++) {
                    std::string tempStr = *it;
                    app_error(" %s %s\n", "Classifier params is not set : ", tempStr.c_str());
                }
                return APP_FAILURE;
            }
        } else {
            app_error(" %s \n", "Classifier params is not set ");
            return APP_FAILURE;
        }
    } else if (PostProcessNetworkType_Segmentation == m_postprocessInitParams.networkType) {
        if (configyml["segmentation-params"]) {
            std::set<std::string> mandatoryString{"segmentation-threshold", "segmentation-output-order"};
            for (YAML::const_iterator itr = configyml["segmentation-params"].begin();
                 itr != configyml["segmentation-params"].end(); ++itr) {
                std::string paramKey = itr->first.as<std::string>();
                if (paramKey == "segmentation-threshold") {
                    mandatoryString.erase(paramKey);
                    m_postprocessInitParams.segmentationThreshold = (float)itr->second.as<float>();
                    app_debug(" %s %.3f\n", "Segmentation params segmentation-threshold is ",
                              m_postprocessInitParams.segmentationThreshold);
                } else if (paramKey == "segmentation-output-order") {
                    mandatoryString.erase(paramKey);
                    m_postprocessInitParams.segmentationOutputOrder = (int)itr->second.as<int>();
                    app_debug(" %s %d\n", "Segmentation params segmentation-output-order is ",
                              m_postprocessInitParams.segmentationOutputOrder);
                } else {
                    app_debug(" %s %s\n", "Segmentation params Unknown parameter is ", paramKey.c_str());
                }
            }
            if (mandatoryString.size() > 0) {
                for (auto it = mandatoryString.begin(); it != mandatoryString.end(); it++) {
                    std::string tempStr = *it;
                    app_error(" %s %s\n", "Segmentation params is not set : ", tempStr.c_str());
                }
                return APP_FAILURE;
            }
        } else {
            app_error(" %s\n", "Segmentation params is not set ");
            return APP_FAILURE;
        }
    } else if (PostProcessNetworkType_Rtmpose == m_postprocessInitParams.networkType) {
        if (configyml["rtmpose-params"]) {
            std::set<std::string> mandatoryString{"scale_x", "scale_y", "scoreThreshold"};
            for (YAML::const_iterator itr = configyml["rtmpose-params"].begin();
                 itr != configyml["rtmpose-params"].end(); ++itr) {
                std::string paramKey = itr->first.as<std::string>();
                if (paramKey == "scale_x") {
                    mandatoryString.erase(paramKey);
                    m_postprocessInitParams.rtmposeParams.scale_x = (float)itr->second.as<float>();
                    app_debug(" %s %.3f\n", "rtmpose-params params segmentation-threshold is ",
                              m_postprocessInitParams.segmentationThreshold);
                } else if (paramKey == "scale_y") {
                    mandatoryString.erase(paramKey);
                    m_postprocessInitParams.rtmposeParams.scale_y = (float)itr->second.as<float>();
                    app_debug(" %s %d\n", "rtmpose-params params segmentation-output-order is ",
                              m_postprocessInitParams.segmentationOutputOrder);
                } else if (paramKey == "scoreThreshold") {
                    m_postprocessInitParams.rtmposeParams.scoreThreshold = (float)itr->second.as<float>();
                } else {
                    app_error(" %s %s\n", "rtmpose-params params Unknown parameter is ", paramKey.c_str());
                }
            }
        } else {
            app_error(" %s\n", "rtmpose params is not set ");
            return APP_FAILURE;
        }
    }

    if (m_postprocessInitParams.labelsFilePath.size() > 0) {
        app_debug(" %s\n", "will parse label file ");
        int labelLength = parseLabelsFile(m_postprocessInitParams.labelsFilePath);
        if (0 != labelLength) {
            app_debug(" %s\n", "parse label faile ");
        } else {
            app_debug(" %s %d\n", " the label length is : ", (int)m_postLabels.size());
        }
    }

    return APP_SUCCESS;
}

app_ret PostprocessElement::Init() {
    app_ret err = parseConfigFile();
    if (APP_SUCCESS != err) {
        app_error(" %s\n", "parseConfigFile error ");
        return err;
    }
    ometaPool = new MetaPool<CObjectMeta>(200 * MAX_VIDEO_GRP_NUM);
    softmaxPerformance = new PerformanceStatic(mName + "_softmaxPerformance", PERF_STATIC_SEGMENT);
    argmaxPerformance = new PerformanceStatic(mName + "_argmaxPerformance", PERF_STATIC_SEGMENT);
    detectionPerformance = new PerformanceStatic(mName + "_detectionPerformance", PERF_STATIC_SEGMENT);
    pdspOperationObject = std::make_unique<DspOp>(&softmaxPerformance, &argmaxPerformance, &detectionPerformance,
                                                  m_postprocessInitParams.dspID, m_dieIndex);

    return APP_SUCCESS;
}

app_ret PostprocessElement::Wait() {
    app_debug(" %s \n", " PostprocessElement wait start ");
    app_debug(" %s \n", " PostprocessElement wait end ");
    return APP_SUCCESS;
}

app_ret PostprocessElement::perfStat() {
    softmaxPerformance->performanceStaticReport();
    argmaxPerformance->performanceStaticReport();
    detectionPerformance->performanceStaticReport();
    return APP_SUCCESS;
}

app_ret PostprocessElement::Finish() {
    app_debug(" %s %s\n", mName.c_str(), " PostprocessElement::Finish start ");
    delete softmaxPerformance;
    delete argmaxPerformance;
    delete detectionPerformance;
    bool testResult = true;
    for (auto it = m_classifyOutput.begin(); it != m_classifyOutput.end(); it++) {
        uint tempPadindex = it->first;
        std::vector<classifyGoldData> tempOutput = it->second;
        for (int index = 0; index < tempOutput.size() && index < m_goldenData.size(); index++) {
            app_debug(" %s %d %s %d %s [%d, %f] %s [%d, %f] %s [%d, %f]\n", " the padIndex : ", tempPadindex,
                      " , the frameIndex : ", index, " , goldData is : ", m_goldenData[index].classID,
                      m_goldenData[index].confidence, " , the output is : ", tempOutput[index].classID,
                      tempOutput[index].confidence,
                      ", the tolerance is : ", m_goldenData[index].classID - tempOutput[index].classID,
                      m_goldenData[index].confidence - tempOutput[index].confidence);
            if (1 == testResult && (m_goldenData[index].classID - tempOutput[index].classID != 0 ||
                                    abs(m_goldenData[index].confidence - tempOutput[index].confidence) > 0.2)) {
                testResult = false;
            }
        }
    }
    if (!testResult) {
        fstream tempStrem("/root/result_output/case_result.txt", ios::app);
        tempStrem << "this case test  failed\n";
        tempStrem.close();
    } else {
        fstream tempStrem("/root/result_output/case_result.txt", ios::app);
        tempStrem << "this case test  success\n";
        tempStrem.close();
    }

    app_debug(" %s \n", " will close dsp and release some sys fd ");
    pdspOperationObject->closeDspDevice();
    delete ometaPool;
    app_debug(" %s %s\n", mName.c_str(), " PostprocessElement::Finish end ");
    freeNumaNode(this, sizeof(PostprocessElement));
    return APP_SUCCESS;
}

app_ret PostprocessElement::classifyAttachBatchMeta(CInferOutputMeta *inferOutputMeta, std::vector<int> &maxIndex,
                                                    std::vector<float> &maxConfidenc) {
    app_debug(" %s\n", "classifyAttachBatchMeta enter ");
    int iFrameSize = inferOutputMeta->parentFrameMeta.size();
    int iMaxSize = maxIndex.size();
    int iConfidenceSize = maxConfidenc.size();
    app_debug(" %s [ %d, %d, %d ]\n", " the [ frameSize, maxValueSize, confidenceSize] is:  ", iFrameSize, iMaxSize,
              iConfidenceSize);
    for (int iframeMetaIndex = 0; iframeMetaIndex < iFrameSize; iframeMetaIndex++) {
        CFrameMeta *oriFrameMeta = inferOutputMeta->parentFrameMeta[iframeMetaIndex];
        CObjectMeta *objMeta = ometaPool->allocate();
        objMeta->pool = ometaPool;
        objMeta->classId = maxIndex[iframeMetaIndex];
        objMeta->detectorConfidence = maxConfidenc[iframeMetaIndex];
        objMeta->parentFrameMeta = oriFrameMeta;
        if (objMeta->classId < m_postLabels.size()) {
            objMeta->objLable = m_postLabels[objMeta->classId];
            app_debug(" %s %s\n", "the label is:  ", objMeta->objLable.c_str());
        }
        objMeta->detectorBboxInfo.left = -1.0;
        objMeta->detectorBboxInfo.top = -1.0;
        objMeta->detectorBboxInfo.width = -1.0;
        objMeta->detectorBboxInfo.height = -1.0;
        objMeta->objType = PL_OBJ_CLASSIFY;
        app_info(" %s [ %d, %.5f ]\n", " the [ classID, confidence] is:  ", objMeta->classId,
                 objMeta->detectorConfidence);
        if (oriFrameMeta->objs.size() > MAX_OBJMETA_PER_FRAME) {
            objMeta->release();
            continue;
        }
        oriFrameMeta->objs.push_back(objMeta);
        classifyGoldData tempGold;
        tempGold.classID = objMeta->classId;
        tempGold.confidence = objMeta->detectorConfidence;
        m_classifyOutput[oriFrameMeta->padIndex].push_back(tempGold);
    }
    app_debug(" %s\n", "classifyAttachBatchMeta end ");
    return APP_SUCCESS;
}

app_ret PostprocessElement::rtmposeAttachBatchMeta(CInferOutputMeta *inferOutputMeta,
                                                   PostProcessInitParams &_postprocessInitParams,
                                                   std::vector<std::vector<RtmPosePointInfo>> &outputVec) {
    int ibatchSize = outputVec.size();
    int iFrameSize = inferOutputMeta->parentFrameMeta.size();
    int iparentObjSize = inferOutputMeta->parentObjMeta.size();
    int iOutputSize = 0;
    app_debug(" %s\n", "rtmposeAttachBatchMeta enter ");
    app_debug(" %s %d %s %d %s %d\n", "the output Size is : ", iOutputSize, "the FrameSize is : ", iFrameSize,
              "the parent obj Size is : ", iparentObjSize);
    ES_ASSERT(1 == ibatchSize, "the post parent Obj meta size is : %d  ibatchSize %d iparentObjSize %d", iFrameSize,
              ibatchSize, iparentObjSize);

    if (0 == iparentObjSize) {
        for (int batch = 0; batch < ibatchSize; batch++) {
            iOutputSize = outputVec[batch].size();

            CFrameMeta *oriFrameMeta = inferOutputMeta->parentFrameMeta[batch];
            float aspection = 1.0;
            if (inferOutputMeta->aspectRatio.size() > batch) {
                aspection = inferOutputMeta->aspectRatio[batch];
            } else {
                aspection = 1.0;
            }
            app_debug(" %s  %.3f\n", " the ration is:  ", aspection);

            CObjectMeta *objMeta = ometaPool->allocate();
            objMeta->pool = ometaPool;
            objMeta->parentFrameMeta = oriFrameMeta;
            objMeta->detectorConfidence = 0;
            objMeta->objType = PL_OBJ_RTMPOSE;
            CPoints Point;

            for (int iOutputIdx = 0; iOutputIdx < iOutputSize; iOutputIdx++) {
                RtmPosePointInfo poseOutput = outputVec[batch][iOutputIdx];
                {
                    if (poseOutput.score >= _postprocessInitParams.rtmposeParams.scoreThreshold) {
                        objMeta->rtmPoseInfo.push_back(poseOutput);

                        float x = (poseOutput.x * inferOutputMeta->aspectRatioExtW[batch]) +
                                  inferOutputMeta->aspectRatioExtX[batch];
                        float y = (poseOutput.y * inferOutputMeta->aspectRatioExtH[batch]) +
                                  inferOutputMeta->aspectRatioExtY[batch];
                        Point.x = x;
                        Point.y = y;
                        objMeta->keyPoint.push_back(Point);
                    } else {
                        app_debug(" poseOutput.score = %f <  %f[scoreThreshold]\n", poseOutput.score,
                                  _postprocessInitParams.rtmposeParams.scoreThreshold);
                    }
                    app_debug(
                        " frameMeta->index %d iOutputIdx %d poseOutput x  %d y %d score %f point x %f y%f "
                        "aspectRatioExt{ x %f y %f w %f h %f} \n",
                        oriFrameMeta->index, iOutputIdx, poseOutput.x, poseOutput.y, poseOutput.score, Point.x, Point.y,
                        inferOutputMeta->aspectRatioExtX[batch], inferOutputMeta->aspectRatioExtY[batch],
                        inferOutputMeta->aspectRatioExtW[batch], inferOutputMeta->aspectRatioExtH[batch]);
                }
            }

            if (oriFrameMeta->rtmObjs.size() > MAX_OBJMETA_PER_FRAME) {
                objMeta->release();
                app_error("\n oriFrameMeta->objs.size( %d ) > MAX_OBJMETA_PER_FRAME(%d) ", oriFrameMeta->rtmObjs.size(),
                          MAX_OBJMETA_PER_FRAME);
                continue;
            } else {
                oriFrameMeta->rtmObjs.push_back(objMeta);
            }
        }
    } else {
        for (int batch = 0; batch < ibatchSize; batch++) {
            iOutputSize = outputVec[batch].size();
            // need notify
            CFrameMeta *oriFrameMeta = inferOutputMeta->parentObjMeta[batch]->parentFrameMeta;

            CObjectMeta *objMeta = ometaPool->allocate();
            objMeta->pool = ometaPool;
            objMeta->parentFrameMeta = oriFrameMeta;
            objMeta->detectorConfidence = 0;
            objMeta->objType = PL_OBJ_RTMPOSE;
            CPoints Point;
            for (int iOutputIdx = 0; iOutputIdx < iOutputSize; iOutputIdx++) {
                RtmPosePointInfo poseOutput = outputVec[batch][iOutputIdx];
                {
                    if (poseOutput.score >= _postprocessInitParams.rtmposeParams.scoreThreshold) {
                        objMeta->rtmPoseInfo.push_back(poseOutput);

                        float x = (poseOutput.x * inferOutputMeta->aspectRatioExtW[batch]) +
                                  inferOutputMeta->aspectRatioExtX[batch];
                        float y = (poseOutput.y * inferOutputMeta->aspectRatioExtH[batch]) +
                                  inferOutputMeta->aspectRatioExtY[batch];
                        Point.x = x;
                        Point.y = y;
                        objMeta->keyPoint.push_back(Point);
                    } else {
                        app_debug(" poseOutput.score = %f <  %f[scoreThreshold]\n", poseOutput.score,
                                  _postprocessInitParams.rtmposeParams.scoreThreshold);
                    }
                    app_debug(
                        " frameMeta->index %d iOutputIdx %d poseOutput x  %d y %d score %f point x %f y%f "
                        "aspectRatioExt{ x %f y %f w %f h %f} \n",
                        oriFrameMeta->index, iOutputIdx, poseOutput.x, poseOutput.y, poseOutput.score, Point.x, Point.y,
                        inferOutputMeta->aspectRatioExtX[batch], inferOutputMeta->aspectRatioExtY[batch],
                        inferOutputMeta->aspectRatioExtW[batch], inferOutputMeta->aspectRatioExtH[batch]);
                }
            }

            if (oriFrameMeta->rtmObjs.size() > MAX_OBJMETA_PER_FRAME) {
                objMeta->release();
                app_error("\n oriFrameMeta->objs.size( %d ) > MAX_OBJMETA_PER_FRAME(%d) ", oriFrameMeta->rtmObjs.size(),
                          MAX_OBJMETA_PER_FRAME);
                continue;
            } else {
                oriFrameMeta->rtmObjs.push_back(objMeta);
            }
        }
    }

    return APP_SUCCESS;
}

app_ret PostprocessElement::detectionAttachBatchMeta(CInferOutputMeta *inferOutputMeta,
                                                     PostProcessInitParams &_postprocessInitParams,
                                                     std::vector<std::vector<DetectionOutput>> &outputVec) {
    int ibatchSize = outputVec.size();
    int iFrameSize = inferOutputMeta->parentFrameMeta.size();
    int iparentObjSize = inferOutputMeta->parentObjMeta.size();
    int iOutputSize = 0;
    app_debug(" %s\n", "detectionAttachBatchMeta enter ");
    app_debug(" %s %d %s %d %s %d\n", "the output Size is : ", iOutputSize, "the FrameSize is : ", iFrameSize,
              "the parent obj Size is : ", iparentObjSize);
    ES_ASSERT(iFrameSize == ibatchSize, "the post parent Obj meta size is : %d  ibatchSize %d", iFrameSize, ibatchSize);

    if (0 == iparentObjSize) {
        for (int batch = 0; batch < ibatchSize; batch++) {
            iOutputSize = outputVec[batch].size();
            for (int iOutputIdx = 0; iOutputIdx < iOutputSize; iOutputIdx++) {
                DetectionOutput tempOutput = outputVec[batch][iOutputIdx];
                const int class_id = static_cast<int>(tempOutput.classID);
                if (class_id < 0 || class_id >= static_cast<int>(m_postLabels.size()) ||
                    m_postLabels[class_id].empty()) {
                    continue;
                }

                if (tempOutput.batchID < iFrameSize) {
                    CFrameMeta *oriFrameMeta = inferOutputMeta->parentFrameMeta[tempOutput.batchID];
                    float aspection = 1.0;
                    if (inferOutputMeta->aspectRatio.size() > tempOutput.batchID) {
                        aspection = inferOutputMeta->aspectRatio[tempOutput.batchID];
                    } else {
                        aspection = 1.0;
                    }
                    app_debug(" %s  %.3f\n", " the ration is:  ", aspection);

                    CObjectMeta *objMeta = ometaPool->allocate();
                    objMeta->pool = ometaPool;
                    objMeta->classId = class_id;
                    objMeta->detectorConfidence = (float)tempOutput.score;
                    objMeta->parentFrameMeta = oriFrameMeta;
                    objMeta->objType = PL_OBJ_YOLO;
                    if (objMeta->classId < m_postLabels.size()) {
                        objMeta->objLable = m_postLabels[objMeta->classId];
                        app_debug(" %s %s\n", "the label is:  ", objMeta->objLable.c_str());
                    }

                    if (m_postprocessInitParams.optype) {
                        int tempLeft = tempOutput.left;
                        tempOutput.left = tempOutput.left / m_postprocessInitParams.detectionParams.inputWH.W;
                        tempOutput.w = (tempOutput.w - tempLeft) / m_postprocessInitParams.detectionParams.inputWH.W;
                        int tempTop = tempOutput.top;
                        tempOutput.top = tempOutput.top / m_postprocessInitParams.detectionParams.inputWH.H;
                        tempOutput.h = (tempOutput.h - tempTop) / m_postprocessInitParams.detectionParams.inputWH.H;
                    }

                    if (aspection > 1) {
                        objMeta->detectorBboxInfo.left = tempOutput.left;
                        objMeta->detectorBboxInfo.top = (tempOutput.top - 0.5) * aspection + 0.5;

                        objMeta->detectorBboxInfo.width = tempOutput.w;
                        objMeta->detectorBboxInfo.height = tempOutput.h * aspection;
                        if (objMeta->detectorBboxInfo.height > 1) {
                            objMeta->detectorBboxInfo.height = 1;
                        }

                        if (objMeta->detectorBboxInfo.top < 0) {
                            objMeta->detectorBboxInfo.top = 0;
                        } else if (objMeta->detectorBboxInfo.top > 1) {
                            objMeta->detectorBboxInfo.top = 1;
                        }

                        if (objMeta->detectorBboxInfo.top + objMeta->detectorBboxInfo.height > 1) {
                            objMeta->detectorBboxInfo.height = 1 - objMeta->detectorBboxInfo.top;
                        }

                        // objMeta->detectorBboxInfo.left = tempOutput.left;
                        // objMeta->detectorBboxInfo.top = tempOutput.top;
                        // objMeta->detectorBboxInfo.width = tempOutput.w;
                        // objMeta->detectorBboxInfo.height = tempOutput.h;
                    } else {
                        objMeta->detectorBboxInfo.left = (tempOutput.left - 0.5) / aspection + 0.5;
                        objMeta->detectorBboxInfo.top = tempOutput.top;
                        objMeta->detectorBboxInfo.width = tempOutput.w / aspection;
                        if (objMeta->detectorBboxInfo.width > 1) {
                            objMeta->detectorBboxInfo.width = 1;
                        }

                        objMeta->detectorBboxInfo.height = tempOutput.h;

                        if (objMeta->detectorBboxInfo.left < 0) {
                            objMeta->detectorBboxInfo.left = 0;
                        } else if (objMeta->detectorBboxInfo.left > 1) {
                            objMeta->detectorBboxInfo.left = 1;
                        }

                        if (objMeta->detectorBboxInfo.left + objMeta->detectorBboxInfo.width > 1) {
                            objMeta->detectorBboxInfo.width = 1 - objMeta->detectorBboxInfo.left;
                        }

                        // objMeta->detectorBboxInfo.left = tempOutput.left;
                        // objMeta->detectorBboxInfo.top = tempOutput.top;
                        // objMeta->detectorBboxInfo.width = tempOutput.w;
                        // objMeta->detectorBboxInfo.height = tempOutput.h;
                    }

                    app_debug(
                        " %s [ %d, %.5f ] , the origin bbox is : [% .4f, % .4f, % "
                        ".4f, % .4f] \n",
                        " the origin [ classID, confidence] is:  ", objMeta->classId, objMeta->detectorConfidence,
                        tempOutput.left, tempOutput.top, tempOutput.w, tempOutput.h);

                    app_debug(
                        "  %s [index %d  %d, %.5f ] , the bbox is : [% .4f, % .4f, % .4f, % "
                        ".4f] \n",
                        " the [ classID, confidence] is:  ", oriFrameMeta->index, objMeta->classId,
                        objMeta->detectorConfidence, objMeta->detectorBboxInfo.left, objMeta->detectorBboxInfo.top,
                        objMeta->detectorBboxInfo.width, objMeta->detectorBboxInfo.height);

                    if (oriFrameMeta->objs.size() > MAX_OBJMETA_PER_FRAME) {
                        objMeta->release();
                        continue;
                    } else {
                        oriFrameMeta->objs.push_back(objMeta);
                        app_debug(
                            " %s [ %d, %.5f ] , the bbox is : [% .4f, % .4f, % .4f, % "
                            ".4f] \n",
                            " the [ classID, confidence] is:  ", objMeta->classId, objMeta->detectorConfidence,
                            objMeta->detectorBboxInfo.left, objMeta->detectorBboxInfo.top,
                            objMeta->detectorBboxInfo.width, objMeta->detectorBboxInfo.height);
                    }
                }
            }
        }
    } else {
        for (int batch = 0; batch < ibatchSize; batch++) {
            iOutputSize = outputVec[batch].size();
            for (int iOutputIdx = 0; iOutputIdx < iOutputSize; iOutputIdx++) {
                DetectionOutput tempOutput = outputVec[batch][iOutputIdx];
                const int class_id = static_cast<int>(tempOutput.classID);
                if (class_id < 0 || class_id >= static_cast<int>(m_postLabels.size()) ||
                    m_postLabels[class_id].empty()) {
                    continue;
                }
                if (tempOutput.batchID < iparentObjSize) {
                    CFrameMeta *oriFrameMeta = inferOutputMeta->parentObjMeta[tempOutput.batchID]->parentFrameMeta;

                    CObjectMeta *objMeta = ometaPool->allocate();
                    objMeta->pool = ometaPool;
                    objMeta->classId = class_id;
                    objMeta->detectorConfidence = (float)tempOutput.score;
                    objMeta->parentFrameMeta = oriFrameMeta;
                    objMeta->objType = PL_OBJ_YOLO;
                    if (objMeta->classId < m_postLabels.size()) {
                        objMeta->objLable = m_postLabels[objMeta->classId];
                        app_debug(" %s %s\n", "the label is:  ", objMeta->objLable.c_str());
                    }

                    objMeta->detectorBboxInfo.left = tempOutput.left;
                    objMeta->detectorBboxInfo.top = tempOutput.top;
                    objMeta->detectorBboxInfo.width = tempOutput.w;
                    objMeta->detectorBboxInfo.height = tempOutput.h;

                    app_debug(
                        " %s [ %d, %.5f ] , the bbox is : [% .4f, % .4f, % .4f, % "
                        ".4f] \n",
                        " the [ classID, confidence] is:  ", objMeta->classId, objMeta->detectorConfidence,
                        objMeta->detectorBboxInfo.left, objMeta->detectorBboxInfo.top, objMeta->detectorBboxInfo.width,
                        objMeta->detectorBboxInfo.height);
                    if (oriFrameMeta->objs.size() > MAX_OBJMETA_PER_FRAME) {
                        objMeta->release();
                        continue;
                    } else {
                        // error  need notify
                        oriFrameMeta->objs.push_back(objMeta);
                        app_debug(
                            " %s [ %d, %.5f ] , the bbox is[% .4f, % .4f, % .4f, % "
                            ".4f] \n",
                            " the [ classID, confidence] is:  ", objMeta->classId, objMeta->detectorConfidence,
                            objMeta->detectorBboxInfo.left, objMeta->detectorBboxInfo.top,
                            objMeta->detectorBboxInfo.width, objMeta->detectorBboxInfo.height);
                    }
                }
            }
        }
    }

    return APP_SUCCESS;
}

app_ret PostprocessElement::ProcessData(CBaseMeta *baseMeta, CElement const *privious) {
    if (baseMeta->eosFlag) {
        app_debug("ElementInner eos PostprocessElement");
        return APP_SUCCESS;
    }
    if (!m_cpuSetFlag) {
        // cpu_set_t cpuset;
        // CPU_ZERO(&cpuset);
        // CPU_SET(m_cpuID, &cpuset);
        // pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
        // getCpuNumaID(__func__);
        set_thread_affinity(m_dieIndex);
        m_cpuSetFlag = true;
    }
    app_debug(" in: %s\n", "ProcessData enter ");

    CBatchMeta *batchMeta = (CBatchMeta *)(baseMeta);

    if (ESPOSTDSP == m_postprocessInitParams.optype) {
        // CBatchMeta *batchMeta = (CBatchMeta *)(baseMeta);
        app_debug(" %s %d\n", "post process data size is : ", int(batchMeta->m_inferOutputMetaVec.size()));
        for (size_t index = 0; index < batchMeta->m_inferOutputMetaVec.size(); index++) {
            CInferOutputMeta *inferOutputMeta = batchMeta->m_inferOutputMetaVec[index];
            if (inferOutputMeta->uniqueID != m_postprocessInitParams.m_targetInferID) {
                app_debug(" %s %d %s %d\n", "the inferOutputMeta->ID is : ", inferOutputMeta->uniqueID,
                          "the initParams ID is : ", m_postprocessInitParams.m_targetInferID);
                continue;
            }
            app_debug(" %s %d\n", "networkType is : ", m_postprocessInitParams.networkType);
            switch (m_postprocessInitParams.networkType) {
                case PostProcessNetworkType_Detector: {  // 0. 检测
                    std::vector<std::vector<DetectionOutput>> outputVec;
                    pdspOperationObject->detectionPostprocess(inferOutputMeta, m_postprocessInitParams.detectionParams,
                                                              outputVec);
                    detectionAttachBatchMeta(inferOutputMeta, m_postprocessInitParams, outputVec);
                } break;
                case PostProcessNetworkType_Classifier: {  // 1: 分类;
                    std::vector<int> maxIndex;
                    std::vector<float> maxConfidenc;
                    pdspOperationObject->classifyPostprocess(inferOutputMeta, m_postprocessInitParams.softmaxScale, 1,
                                                             maxIndex, maxConfidenc);
                    classifyAttachBatchMeta(inferOutputMeta, maxIndex, maxConfidenc);
                } break;
                case PostProcessNetworkType_Segmentation:  // 2: 分割
                    pdspOperationObject->segmentPostprocess(inferOutputMeta);
                    break;
                case PostProcessNetworkType_Rtmpose: {
                    std::vector<std::vector<RtmPosePointInfo>> outputVec;
                    pdspOperationObject->rtmposePostprocess(inferOutputMeta, m_postprocessInitParams.rtmposeParams,
                                                            outputVec);
                    rtmposeAttachBatchMeta(inferOutputMeta, m_postprocessInitParams, outputVec);
                } break;
            }
        }
    } else if (ESPOSTCPU == m_postprocessInitParams.optype) {
        // CBatchMeta *batchMeta = (CBatchMeta *)(baseMeta);
        int outputSize = batchMeta->m_inferOutputMetaVec.size();
        app_debug(" %s %d\n", "post process data size is : ", int(batchMeta->m_inferOutputMetaVec.size()));
        for (size_t index = 0; index < outputSize; index++) {
            CInferOutputMeta *inferOutputMeta = batchMeta->m_inferOutputMetaVec[index];
            int iParentObjSize = inferOutputMeta->parentObjMeta.size();
            ES_ASSERT(iParentObjSize == 0, "the post parent Obj meta size is : %d", iParentObjSize);

            if (NULL == inferOutputMeta) {
                continue;
            }
            if (inferOutputMeta->uniqueID != m_postprocessInitParams.m_targetInferID) {
                app_error(" %s %d %s %d\n", "the inferOutputMeta->ID is : ", inferOutputMeta->uniqueID,
                          "the initParams ID is : ", m_postprocessInitParams.m_targetInferID);
                continue;
            }
            app_debug(" %s %d\n", "networkType is : ", m_postprocessInitParams.networkType);
            switch (m_postprocessInitParams.networkType) {
                case PostProcessNetworkType_Detector: {  // 0. 检测
                    std::vector<std::vector<DetectionOutput>> outputVec;
                    cpuProcessData.detectionPostprocess(inferOutputMeta, outputVec);
                    detectionAttachBatchMeta(inferOutputMeta, m_postprocessInitParams, outputVec);
                } break;
                case PostProcessNetworkType_Classifier: {  // 1: 分类;
                    std::vector<int> maxIndex;
                    std::vector<float> maxConfidenc;
                    cpuProcessData.classifyArgmax(inferOutputMeta, maxIndex, maxConfidenc);
                    classifyAttachBatchMeta(inferOutputMeta, maxIndex, maxConfidenc);
                } break;
            }
        }
    }
    app_debug(" out: %s\n", "ProcessData end ");
    return APP_SUCCESS;
}

extern "C" CElement *createEsPostProcessElement(const char *name, const char *configFile, int dieIndex) {
    return new (bindNumaNode(dieIndex, sizeof(PostprocessElement))) PostprocessElement(name, configFile, dieIndex);
}
