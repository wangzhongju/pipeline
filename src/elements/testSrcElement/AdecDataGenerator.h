#ifndef _ADEC_DATA_GENERATOR_H__
#define _ADEC_DATA_GENERATOR_H__

#include "IDataGenerator.h"
#include "audio.h" // For AUDIO_STREAM_S

// Definition from original testSrcElement.h
struct AdecDataInfo {
    string source;
};

class AdecDataGenerator : public IDataGenerator {
public:
    AdecDataGenerator() = default;
    ~AdecDataGenerator() = default;

    app_ret Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) override;
    CBaseMeta* GenerateData(int frameIndex, FILE*& fp) override;

private:
    CAudioPacketMeta* prepareAudioPacketInputData(FILE* pfd);

private:
    AdecDataInfo m_adecDataInfo;
    uint16_t m_loopNum;
};

#endif // _ADEC_DATA_GENERATOR_H__
