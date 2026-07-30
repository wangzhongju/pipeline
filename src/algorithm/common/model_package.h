#pragma once

#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace algorithm::cdky::model_package {

using FileMap = std::map<std::string, std::vector<unsigned char>>;

struct Package {
    FileMap files;
    nlohmann::json manifest;
    std::string root_config_name;
    nlohmann::json root_config;
};

bool isEncryptedPackage(const std::string& package_path);

bool isNoKeyPackage(const std::string& package_path);

bool isPackage(const std::string& package_path);

bool loadEncryptedPackage(const std::string& package_path,
                          Package& package,
                          std::string* error_message = nullptr);

bool loadNoKeyPackage(const std::string& package_path,
                      Package& package,
                      std::string* error_message = nullptr);

bool loadPackage(const std::string& package_path,
                 Package& package,
                 std::string* error_message = nullptr);

bool writePackageFilesToDirectory(const Package& package,
                                  const std::string& output_dir,
                                  std::string* error_message = nullptr);

std::string normalizeEntryName(const std::string& entry_name);

const std::vector<unsigned char>* findFile(const FileMap& files,
                                           const std::string& entry_name);

void wipePackageFiles(Package* package);

}  // namespace algorithm::cdky::model_package
