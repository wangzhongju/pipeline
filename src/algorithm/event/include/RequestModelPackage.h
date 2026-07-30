#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace algorithm::cdky::event_request_package {

bool isEncryptedPackage(const std::string& packagePath);

bool loadRootConfigJson(const std::string& packagePath,
                        nlohmann::json& rootConfig,
                        std::string* errorMessage = nullptr);

} // namespace algorithm::cdky::event_request_package
