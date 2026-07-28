#ifndef _SAVE_DATA_GENERATOR_H__
#define _SAVE_DATA_GENERATOR_H__

#include "IDataGenerator.h"

// Definition from original testSrcElement.h
struct SaveDataInputDataInfo {
    int feature_shape;
    string source;
};

class SaveDataGenerator : public IDataGenerator {
public:
    SaveDataGenerator() = default;
    ~SaveDataGenerator() = default;

    app_ret Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) override;
    CBaseMeta* GenerateData(int frameIndex, FILE*& fp) override;

private:
    CFrameMeta* prepareSaveDataInputData();

private:
    SaveDataInputDataInfo m_saveDataInputDataInfo;
    uint16_t m_loopNum;
};

#endif // _SAVE_DATA_GENERATOR_H__
