#define PL_LOG_ID PL_LOG_TESTSRC
#include "DataGeneratorFactory.h"

#include "AdecDataGenerator.h"
#include "CompareFaceDataGenerator.h"
#include "EncodeDataGenerator.h"
#include "FaceSelectDataGenerator.h"
#include "InferDataGenerator.h"
#include "MuxTestDataGenerator.h"
#include "OsdDataGenerator.h"
#include "PostprocessDataGenerator.h"
#include "SaveDataGenerator.h"
#include "TTSDataGenerator.h"
#include "TrackerDataGenerator.h"
#include "VideoGridDataGenerator.h"
// Other generator headers will be included here as they are created

#include "element.h"  // For app_error, app_info

std::unique_ptr<IDataGenerator> DataGeneratorFactory::Create(const YAML::Node& config) {
    std::vector<std::string> enabledGenerators;

    // A list of all possible generator config sections
    const std::vector<std::string> knownGenerators = {
        "encodeInput",        "faceSelectData",         "create_osd_data",    "create_muxTest_data",
        "createInferInput",   "createPostprocessInput", "createTrackerInput", "saveDataInput",
        "compareFaceInput",   "videogridInput",         "adecDataInput",      "TTSInput",
        "createFramerateFlag"
        // Note: some flags like create_preproc_data were in the .h but not implemented in threadFunc
    };

    for (const auto& name : knownGenerators) {
        if (config[name] && config[name]["enable"] && config[name]["enable"].as<bool>()) {
            enabledGenerators.push_back(name);
        } else if (name == "create_osd_data" && config[name] && config[name].as<bool>()) {
            // Handle simple boolean flags
            enabledGenerators.push_back(name);
        } else if (name == "create_muxTest_data" && config[name] && config[name].as<bool>()) {
            enabledGenerators.push_back(name);
        } else if (name == "createFramerateFlag" && config[name] && config[name].as<bool>()) {
            enabledGenerators.push_back(name);
        }
    }

    // Validate that exactly one generator is enabled
    if (enabledGenerators.empty()) {
        app_error(
            "No data generator enabled in config. Please set 'enable: true' for one of the data source sections.");
        return nullptr;
    }

    if (enabledGenerators.size() > 1) {
        app_error("More than one data generator is enabled in config. Please enable only one.");
        for (const auto& name : enabledGenerators) {
            app_error(" - Enabled generator: %s", name.c_str());
        }
        return nullptr;
    }

    const std::string& generatorName = enabledGenerators[0];
    app_info("Selected data generator: %s", generatorName.c_str());

    // Create the instance based on the name
    if (generatorName == "encodeInput") {
        return std::make_unique<EncodeDataGenerator>();
    }
    if (generatorName == "faceSelectData") {
        return std::make_unique<FaceSelectDataGenerator>();
    }
    if (generatorName == "createInferInput") {
        return std::make_unique<InferDataGenerator>();
    }
    if (generatorName == "createPostprocessInput") {
        return std::make_unique<PostprocessDataGenerator>();
    }
    if (generatorName == "createTrackerInput") {
        return std::make_unique<TrackerDataGenerator>();
    }
    if (generatorName == "create_osd_data") {
        return std::make_unique<OsdDataGenerator>();
    }
    if (generatorName == "create_muxTest_data" || generatorName == "createFramerateFlag") {
        return std::make_unique<MuxTestDataGenerator>();
    }
    if (generatorName == "saveDataInput") {
        return std::make_unique<SaveDataGenerator>();
    }
    if (generatorName == "compareFaceInput") {
        return std::make_unique<CompareFaceDataGenerator>();
    }
    if (generatorName == "videogridInput") {
        return std::make_unique<VideoGridDataGenerator>();
    }
    if (generatorName == "adecDataInput") {
        return std::make_unique<AdecDataGenerator>();
    }
    if (generatorName == "TTSInput") {
        return std::make_unique<TTSDataGenerator>();
    }
    // ... more instantiations will be added here as generators are created ...

    app_error("Data generator '%s' is enabled but not yet implemented in the factory.", generatorName.c_str());
    return nullptr;
}
