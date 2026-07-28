#ifndef _COMPARE_FACE_DATA_GENERATOR_H__
#define _COMPARE_FACE_DATA_GENERATOR_H__

#include "IDataGenerator.h"
#include "DataGeneratorUtils.h"

// Definition from original testSrcElement.h
struct CompareFaceInputDataInfo {
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

class CompareFaceDataGenerator : public IDataGenerator {
public:
    CompareFaceDataGenerator() = default;
    ~CompareFaceDataGenerator();

    app_ret Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) override;
    CBaseMeta* GenerateData(int frameIndex, FILE*& fp) override;

private:
    CBatchMeta* prepareCompareFaceData(int frameIndex, FILE* fp);
    app_ret transVectorToFackInferResult(ES_U64& faceFeatureFd, ES_U64& faceFeatureSize, ModelInfo& faceFeatureModelInfo,
                                         std::vector<float>& featureVect);

private:
    CompareFaceInputDataInfo m_compareFaceInputDataInfo;
    uint16_t m_loopNum;
};

#endif // _COMPARE_FACE_DATA_GENERATOR_H__
