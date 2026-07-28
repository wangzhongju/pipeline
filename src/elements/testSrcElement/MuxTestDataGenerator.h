#ifndef _MUX_TEST_DATA_GENERATOR_H__
#define _MUX_TEST_DATA_GENERATOR_H__

#include "IDataGenerator.h"

class MuxTestDataGenerator : public IDataGenerator {
public:
    MuxTestDataGenerator() = default;
    ~MuxTestDataGenerator() = default;

    app_ret Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) override;
    CBaseMeta* GenerateData(int frameIndex, FILE*& fp) override;

private:
    CFrameMeta* prepareMuxTestData(int frameIndex);

private:
    int m_fps;
    uint16_t m_loopNum;
};

#endif // _MUX_TEST_DATA_GENERATOR_H__
