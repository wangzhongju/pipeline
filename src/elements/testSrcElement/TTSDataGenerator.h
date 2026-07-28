#ifndef _TTS_DATA_GENERATOR_H__
#define _TTS_DATA_GENERATOR_H__

#include "IDataGenerator.h"
#include "DataGeneratorUtils.h"

// Definition from original testSrcElement.h
struct TTSInputDataInfo {
    int batchSize;
    std::string inputFileName;
    VB_POOL videoFramePoolId;
    sem_t videoFramePoolSem;
    ES_U32 img_width;
    ES_U32 img_height;
    ES_U32 img_size;
    VB_POOL inferPoolId;
    sem_t inferPoolSem;
    int inferBatchSize;
    int featureShape;
};

class TTSDataGenerator : public IDataGenerator {
public:
    TTSDataGenerator() = default;
    ~TTSDataGenerator();

    app_ret Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) override;
    CBaseMeta* GenerateData(int frameIndex, FILE*& fp) override;

private:
    CBatchMeta* prepareTTSData(int frameIndex, FILE* fp);

private:
    TTSInputDataInfo m_ttsInputDataInfo;
    uint16_t m_loopNum;
};

#endif // _TTS_DATA_GENERATOR_H__
