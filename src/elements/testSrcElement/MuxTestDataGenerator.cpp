#define PL_LOG_ID PL_LOG_TESTSRC
#include "MuxTestDataGenerator.h"

#include "batch_meta.h"  // For CFrameMeta

CFrameMeta* MuxTestDataGenerator::prepareMuxTestData(int frameIndex) {
    CFrameMeta* frameMeta = new CFrameMeta();
    frameMeta->index = frameIndex;
    frameMeta->pts = frameIndex * (1000 / m_fps);
    return frameMeta;
}

app_ret MuxTestDataGenerator::Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) {
    m_fps = fps;
    m_loopNum = loopNum;
    // This generator has no specific config, so Init is simple.
    return APP_SUCCESS;
}

CBaseMeta* MuxTestDataGenerator::GenerateData(int frameIndex, FILE*& fp) {
    // This generator does not use a file.
    CFrameMeta* frameMeta = prepareMuxTestData(frameIndex);
    if (frameIndex == m_loopNum) {
        frameMeta->eosFlag = true;
    }
    return static_cast<CBaseMeta*>(frameMeta);
}
