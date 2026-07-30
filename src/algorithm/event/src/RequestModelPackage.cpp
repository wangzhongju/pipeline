#include "RequestModelPackage.h"

#include "common/model_package.h"

namespace algorithm::cdky::event_request_package {

bool isEncryptedPackage(const std::string& packagePath) {
    return model_package::isEncryptedPackage(packagePath);
}

bool loadRootConfigJson(const std::string& packagePath,
                        nlohmann::json& rootConfig,
                        std::string* errorMessage) {
    model_package::Package package;
    if (!model_package::loadPackage(packagePath, package, errorMessage)) {
        return false;
    }
    rootConfig = package.root_config;
    model_package::wipePackageFiles(&package);
    return true;
}

}  // namespace algorithm::cdky::event_request_package
