#ifndef _TRACKER_DATA_GENERATOR_H__
#define _TRACKER_DATA_GENERATOR_H__

#include "IDataGenerator.h"
#include "DataGeneratorUtils.h"

// Definition from original testSrcElement.h
struct TrackerInputDataInfo {
    int batchSize;
    std::string inputFileName;
    VB_POOL poolId;
    sem_t poolSem;
    ES_U32 img_width;
    ES_U32 img_height;
    ES_U32 img_size;
};

class TrackerDataGenerator : public IDataGenerator {
public:
    TrackerDataGenerator() = default;
    ~TrackerDataGenerator();

    app_ret Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) override;
    CBaseMeta* GenerateData(int frameIndex, FILE*& fp) override;

private:
    CBatchMeta* prepareTrackerData(int frameIndex, FILE* fp);

private:
    TrackerInputDataInfo m_trackerInputDataInfo;
    uint16_t m_loopNum;
};

#endif // _TRACKER_DATA_GENERATOR_H__
