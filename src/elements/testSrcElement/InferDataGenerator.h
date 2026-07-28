#ifndef _INFER_DATA_GENERATOR_H__
#define _INFER_DATA_GENERATOR_H__

#include "IDataGenerator.h"
#include "DataGeneratorUtils.h"

// Definition from original testSrcElement.h
struct InferInputDataInfo {
    int targetInferID;
    uint8_t batchSize;
    std::string inputFileName;
    VB_POOL pool;
    int size;
    CDims dataDims;
};

class InferDataGenerator : public IDataGenerator {
public:
    InferDataGenerator() = default;
    ~InferDataGenerator() = default;

    app_ret Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) override;
    CBaseMeta* GenerateData(int frameIndex, FILE*& fp) override;

private:
    CBatchMeta* prepareInferInputData();

private:
    InferInputDataInfo m_inferInputDataInfo;
    uint16_t m_loopNum;
};

#endif // _INFER_DATA_GENERATOR_H__
