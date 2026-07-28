#ifndef _DATA_GENERATOR_FACTORY_H__
#define _DATA_GENERATOR_FACTORY_H__

#include "IDataGenerator.h"
#include <yaml-cpp/yaml.h>
#include <memory>
#include <string>
#include <vector>

class DataGeneratorFactory {
public:
    static std::unique_ptr<IDataGenerator> Create(const YAML::Node& config);
};

#endif // _DATA_GENERATOR_FACTORY_H__
