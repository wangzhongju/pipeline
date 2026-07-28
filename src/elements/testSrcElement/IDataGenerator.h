#ifndef _I_DATA_GENERATOR_H__
#define _I_DATA_GENERATOR_H__

#include <yaml-cpp/yaml.h>

#include <cstdio>
#include <string>

#include "base_meta.h"
#include "element.h"
class IDataGenerator {
   public:
    virtual ~IDataGenerator() = default;

    virtual app_ret Init(const YAML::Node& config, const std::string& vbName, int fps, uint16_t loopNum) = 0;
    virtual CBaseMeta* GenerateData(int frameIndex, FILE*& fp) = 0;
};

#endif  // _I_DATA_GENERATOR_H__
