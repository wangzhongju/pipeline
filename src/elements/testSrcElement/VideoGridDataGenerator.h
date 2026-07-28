#ifndef _VIDEO_GRID_DATA_GENERATOR_H__
#define _VIDEO_GRID_DATA_GENERATOR_H__

#include "IDataGenerator.h"
#include "DataGeneratorUtils.h"

class VideoGridDataGenerator : public IDataGenerator {
public:
    VideoGridDataGenerator() = default;
    ~VideoGridDataGenerator();

    app_ret Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) override;
    CBaseMeta* GenerateData(int frameIndex, FILE*& fp) override;

private:
    VideoGridInputDataInfo m_videogridInputDataInfo;
    uint16_t m_loopNum;
};

#endif // _VIDEO_GRID_DATA_GENERATOR_H__
