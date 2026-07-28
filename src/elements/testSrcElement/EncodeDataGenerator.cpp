#define PL_LOG_ID PL_LOG_TESTSRC
#include "EncodeDataGenerator.h"

app_ret EncodeDataGenerator::Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) {
    m_fps = fps;
    m_loopNum = loopNum;

    auto encodeConfig = config["encodeInput"];
    m_encodeInputDataInfo.filepath = encodeConfig["filepath"].as<std::string>();
    m_encodeInputDataInfo.width = encodeConfig["width"].as<int>();
    m_encodeInputDataInfo.height = encodeConfig["height"].as<int>();
    m_encodeInputDataInfo.dateType = encodeConfig["datatype"].as<int>();

    VB_POOL_CONFIG_S poolCfg = {0};
    poolCfg.blkCnt = 30;
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

CBaseMeta* EncodeDataGenerator::GenerateData(int frameIndex, FILE*& fp) {
    CBaseMeta* baseMeta = nullptr;
    if (frameIndex == m_loopNum) {
        if (m_encodeInputDataInfo.dateType <= 1) {  // frame format eos
            auto* frameMeta = new CFrameMeta;
            frameMeta->eosFlag = true;
            frameMeta->mMetaType = FRAME_META;
            baseMeta = static_cast<CBaseMeta*>(frameMeta);
        } else {  // batch format eos
            auto* batchMeta = new CBatchMeta;
            batchMeta->eosFlag = true;
            batchMeta->mMetaType = BATCH_META;
            baseMeta = static_cast<CBaseMeta*>(batchMeta);
        }
        if (NULL != fp) {
            fclose(fp);
            fp = NULL;
        }
    } else {
        if (fp == NULL) {
            fp = fopen(m_encodeInputDataInfo.filepath.c_str(), "r");
        }
        if (fp == NULL) {
            app_error("Failed to open file: %s", m_encodeInputDataInfo.filepath.c_str());
            return nullptr;
        }
        baseMeta = prepareEncodeInputData(m_encodeInputDataInfo, m_fps, frameIndex, fp);
    }
    return baseMeta;
}
