#define PL_LOG_ID PL_LOG_TESTSRC
#include "OsdDataGenerator.h"

OsdDataGenerator::~OsdDataGenerator() {
    if (m_encodeInputDataInfo.pool) {
        PL_ES_VB_DestroyPool(m_encodeInputDataInfo.pool);
    }
}

app_ret OsdDataGenerator::Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) {
    m_fps = fps;
    m_loopNum = loopNum;

    // OSD data generation uses the 'encodeInput' config section in the original code
    auto encodeConfig = config["encodeInput"];
    m_encodeInputDataInfo.filepath = encodeConfig["filepath"].as<std::string>();
    m_encodeInputDataInfo.width = encodeConfig["width"].as<int>();
    m_encodeInputDataInfo.height = encodeConfig["height"].as<int>();

    VB_POOL_CONFIG_S poolCfg = {0};
    poolCfg.blkCnt = 10;
    poolCfg.blkSize = m_encodeInputDataInfo.width * m_encodeInputDataInfo.height * 3 / 2;
    poolCfg.enRemapMode = SYS_CACHE_MODE_NOCACHE;
    memcpy(poolCfg.mmzName, vbName.c_str(), vbName.length());
    ES_S32 ret = PL_ES_VB_CreatePool(&poolCfg, &m_encodeInputDataInfo.pool);
    app_debug("%s %d\n", "create pool ret is :", (int)ret);
    if (ret != ES_SUCCESS) {
        return APP_FAILURE;
    }
    return APP_SUCCESS;
}

CBaseMeta* OsdDataGenerator::GenerateData(int frameIndex, FILE*& fp) {
    CFrameMeta* frameMeta = nullptr;

    if (frameIndex == m_loopNum) {
        frameMeta = new CFrameMeta;
        frameMeta->eosFlag = true;
    } else {
        if (fp == NULL) {
            fp = fopen(m_encodeInputDataInfo.filepath.c_str(), "r");
            if (fp == NULL) {
                app_error("Failed to open file: %s", m_encodeInputDataInfo.filepath.c_str());
                return nullptr;
            }
        }
        CBaseMeta* baseMeta = prepareEncodeInputData(m_encodeInputDataInfo, m_fps, frameIndex, fp);
        if (ES_NULL == baseMeta) {
            return nullptr;
        }
        frameMeta = static_cast<CFrameMeta*>(baseMeta);

        for (int k = 0; k < 20; k++) {
            CObjectMeta* obj = new CObjectMeta;
            obj->classId = 1;
            obj->detectorBboxInfo.top = (k / 5 + 1) * 0.15;
            obj->detectorBboxInfo.left = (k % 5) * 0.15;
            obj->detectorBboxInfo.width = 0.1;
            obj->detectorBboxInfo.height = 0.1;
            obj->objLable = "osd 测试";
            obj->detectorConfidence = 0.8878;
            frameMeta->objs.push_back(obj);
        }
    }

    frameMeta->mMetaType = FRAME_META;
    return static_cast<CBaseMeta*>(frameMeta);
}