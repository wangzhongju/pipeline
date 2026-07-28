#ifndef _FACESELECT_DATA_GENERATOR_H__
#define _FACESELECT_DATA_GENERATOR_H__

#include "DataGeneratorUtils.h"
#include "IDataGenerator.h"

class FaceSelectDataGenerator : public IDataGenerator {
   public:
    FaceSelectDataGenerator() = default;
    ~FaceSelectDataGenerator() = default;

    app_ret Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) override;
    CBaseMeta* GenerateData(int frameIndex, FILE*& fp) override;

   private:
    CBatchMeta* prepareFaceselectTestData(int frameIndex, FILE*& fp);

   private:
    EncodeInputDataInfo m_encodeInputDataInfo;
    int m_fps;
    uint16_t m_loopNum;
};

#endif  // _FACESELECT_DATA_GENERATOR_H__
