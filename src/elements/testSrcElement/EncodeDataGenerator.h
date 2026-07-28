#ifndef _ENCODE_DATA_GENERATOR_H__
#define _ENCODE_DATA_GENERATOR_H__

#include "IDataGenerator.h"
#include "DataGeneratorUtils.h"

class EncodeDataGenerator : public IDataGenerator {
public:
    EncodeDataGenerator() = default;
    ~EncodeDataGenerator() = default;

    app_ret Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) override;
    CBaseMeta* GenerateData(int frameIndex, FILE*& fp) override;

private:
    EncodeInputDataInfo m_encodeInputDataInfo;
    int m_fps;
    uint16_t m_loopNum;
};

#endif // _ENCODE_DATA_GENERATOR_H__
