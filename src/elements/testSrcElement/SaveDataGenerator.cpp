
#define PL_LOG_ID PL_LOG_TESTSRC
#include "SaveDataGenerator.h"

#include "batch_meta.h"  // For CFrameMeta

CFrameMeta* SaveDataGenerator::prepareSaveDataInputData() {
    CFrameMeta* frameMeta = new CFrameMeta();
    frameMeta->index = 0;
    frameMeta->source = m_saveDataInputDataInfo.source;
    // The original implementation of feature pushing was commented out.
    // for (int i = 0; i < m_saveDataInputDataInfo.feature_shape; i++) {
    //     frameMeta->feature.push_back(i);
    // }
    return frameMeta;
}

app_ret SaveDataGenerator::Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) {
    m_loopNum = loopNum;
    auto saveDataConfig = config["saveDataInput"];
    m_saveDataInputDataInfo.feature_shape = saveDataConfig["feature_shape"].as<int>();
    m_saveDataInputDataInfo.source = saveDataConfig["source"].as<std::string>();
    return APP_SUCCESS;
}

CBaseMeta* SaveDataGenerator::GenerateData(int frameIndex, FILE*& fp) {
    CFrameMeta* frameMeta;
    if (frameIndex < m_loopNum) {
        frameMeta = prepareSaveDataInputData();
    } else {  // frameIndex == m_loopNum
        frameMeta = new CFrameMeta;
        frameMeta->eosFlag = true;
    }
    return static_cast<CBaseMeta*>(frameMeta);
}
