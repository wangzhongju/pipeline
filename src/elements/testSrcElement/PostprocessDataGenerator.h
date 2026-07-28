#ifndef _POSTPROCESS_DATA_GENERATOR_H__
#define _POSTPROCESS_DATA_GENERATOR_H__

#include "IDataGenerator.h"
#include "DataGeneratorUtils.h"

// Definition from original testSrcElement.h
struct PostprocessInputDataInfo {
    int outputInferID;
    uint8_t batchSize;
    std::vector<std::string> inputFileName;
    std::vector<ES_S32> postprocessInputPool;
    std::vector<int> size;
    std::vector<CDims> dataDims;
    CDataType dataType;
    sem_t postInputSem;
};

class PostprocessDataGenerator : public IDataGenerator {
public:
    PostprocessDataGenerator() = default;
    ~PostprocessDataGenerator();

    app_ret Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) override;
    CBaseMeta* GenerateData(int frameIndex, FILE*& fp) override;

private:
    CBatchMeta* preparePostprocessInputData();

private:
    PostprocessInputDataInfo m_postprocessInputDataInfo;
    uint16_t m_loopNum;
};

#endif // _POSTPROCESS_DATA_GENERATOR_H__
