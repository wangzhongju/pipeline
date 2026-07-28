#ifndef _OSD_DATA_GENERATOR_H__
#define _OSD_DATA_GENERATOR_H__

#include "IDataGenerator.h"
#include "DataGeneratorUtils.h"

class OsdDataGenerator : public IDataGenerator {
public:
    OsdDataGenerator() = default;
    ~OsdDataGenerator();

    app_ret Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) override;
    CBaseMeta* GenerateData(int frameIndex, FILE*& fp) override;

private:
    EncodeInputDataInfo m_encodeInputDataInfo;
    int m_fps;
    uint16_t m_loopNum;
};

#endif // _OSD_DATA_GENERATOR_H__
