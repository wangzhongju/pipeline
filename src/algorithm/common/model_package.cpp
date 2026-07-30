#include "common/model_package.h"

#include <openssl/evp.h>
#include <openssl/sha.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace algorithm::cdky::model_package {
namespace {

constexpr unsigned char kMagic[8] = {'E', 'D', 'P', 'K', 'G', '0', '0', '1'};
constexpr unsigned char kNoKeyMagic[8] = {'E', 'D', 'N', 'O', 'K', 'E', 'Y', '1'};
constexpr uint32_t kVersion = 1;
constexpr uint32_t kFlags = 0;
constexpr size_t kNonceSize = 12;
constexpr size_t kTagSize = 16;
constexpr size_t kAuthenticatedHeaderSize = 8 + 4 + 4 + 8;
constexpr size_t kHeaderSize = kAuthenticatedHeaderSize + kNonceSize + kTagSize;
constexpr size_t kNoKeyHeaderSize = kAuthenticatedHeaderSize;
constexpr const char* kManifestFormat = "edgeDeploy.modelpkg.manifest.v1";

constexpr unsigned char kPackageKey[32] = {
    0x4f, 0x8e, 0x2c, 0x6a, 0x7b, 0x13, 0xd2, 0x09,
    0xf6, 0xc8, 0x5e, 0x91, 0x40, 0xa3, 0x2d, 0xb8,
    0xe5, 0xf1, 0x2c, 0x73, 0xa4, 0xb9, 0xd0, 0x6f,
    0x18, 0xc2, 0xe7, 0xa5, 0x96, 0x0b, 0x3d, 0x44,
};

void setError(std::string* error_message, const std::string& message) {
    if (error_message != nullptr) {
        *error_message = message;
    }
}

bool readFile(const std::string& path, std::vector<unsigned char>& data) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input.is_open()) {
        return false;
    }

    const std::ifstream::pos_type end_pos = input.tellg();
    if (end_pos < 0) {
        return false;
    }
    data.resize(static_cast<size_t>(end_pos));
    input.seekg(0, std::ios::beg);
    if (!data.empty()) {
        input.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size()));
        if (!input) {
            return false;
        }
    }
    return true;
}

bool fileHasMagic(const std::string& path, const unsigned char* expected_magic, size_t magic_size) {
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        return false;
    }
    unsigned char magic[8] = {};
    if (magic_size > sizeof(magic)) {
        return false;
    }
    input.read(reinterpret_cast<char*>(magic), static_cast<std::streamsize>(magic_size));
    return input.gcount() == static_cast<std::streamsize>(magic_size) &&
           std::memcmp(magic, expected_magic, magic_size) == 0;
}

uint16_t readU16(const unsigned char* data) {
    return static_cast<uint16_t>(data[0]) |
           (static_cast<uint16_t>(data[1]) << 8);
}

uint32_t readU32(const unsigned char* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8) |
           (static_cast<uint32_t>(data[2]) << 16) |
           (static_cast<uint32_t>(data[3]) << 24);
}

uint64_t readU64(const unsigned char* data) {
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<uint64_t>(data[i]) << (8 * i);
    }
    return value;
}

std::string baseName(const std::string& entry_name) {
    const size_t pos = entry_name.find_last_of("/\\");
    if (pos == std::string::npos) {
        return entry_name;
    }
    return entry_name.substr(pos + 1);
}

std::string sha256Hex(const std::vector<unsigned char>& data) {
    unsigned char digest[SHA256_DIGEST_LENGTH] = {};
    const unsigned char* input = data.empty() ? reinterpret_cast<const unsigned char*>("") : data.data();
    SHA256(input, data.size(), digest);

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (unsigned char byte : digest) {
        output << std::setw(2) << static_cast<int>(byte);
    }
    return output.str();
}

bool aes256GcmDecrypt(const std::vector<unsigned char>& ciphertext,
                      const unsigned char* nonce,
                      const unsigned char* tag,
                      const unsigned char* aad,
                      size_t aad_size,
                      std::vector<unsigned char>& plaintext,
                      std::string* error_message) {
    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (ctx == nullptr) {
        setError(error_message, "failed to create EVP_CIPHER_CTX");
        return false;
    }

    bool ok = false;
    int len = 0;
    int plaintext_len = 0;
    plaintext.assign(ciphertext.size(), 0);

    do {
        if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1) {
            setError(error_message, "EVP_DecryptInit_ex failed");
            break;
        }
        if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(kNonceSize), nullptr) != 1) {
            setError(error_message, "EVP_CTRL_GCM_SET_IVLEN failed");
            break;
        }
        if (EVP_DecryptInit_ex(ctx, nullptr, nullptr, kPackageKey, nonce) != 1) {
            setError(error_message, "EVP_DecryptInit_ex key/nonce failed");
            break;
        }
        if (aad_size > 0 && EVP_DecryptUpdate(ctx, nullptr, &len, aad, static_cast<int>(aad_size)) != 1) {
            setError(error_message, "EVP_DecryptUpdate AAD failed");
            break;
        }
        if (!ciphertext.empty() &&
            EVP_DecryptUpdate(ctx,
                              plaintext.data(),
                              &len,
                              ciphertext.data(),
                              static_cast<int>(ciphertext.size())) != 1) {
            setError(error_message, "EVP_DecryptUpdate ciphertext failed");
            break;
        }
        plaintext_len = len;
        if (EVP_CIPHER_CTX_ctrl(ctx,
                                EVP_CTRL_GCM_SET_TAG,
                                static_cast<int>(kTagSize),
                                const_cast<unsigned char*>(tag)) != 1) {
            setError(error_message, "EVP_CTRL_GCM_SET_TAG failed");
            break;
        }
        if (EVP_DecryptFinal_ex(ctx, plaintext.data() + plaintext_len, &len) != 1) {
            setError(error_message, "model package authentication failed");
            break;
        }
        plaintext_len += len;
        plaintext.resize(static_cast<size_t>(plaintext_len));
        ok = true;
    } while (false);

    EVP_CIPHER_CTX_free(ctx);
    if (!ok) {
        std::fill(plaintext.begin(), plaintext.end(), 0);
        plaintext.clear();
    }
    return ok;
}

bool parseArchive(const std::vector<unsigned char>& archive,
                  FileMap& files,
                  std::string* error_message) {
    files.clear();
    size_t offset = 0;
    if (archive.size() < 4) {
        setError(error_message, "archive is too small");
        return false;
    }

    const uint32_t file_count = readU32(archive.data());
    offset += 4;

    for (uint32_t index = 0; index < file_count; ++index) {
        if (archive.size() - offset < 2) {
            setError(error_message, "archive entry is missing name length");
            return false;
        }
        const uint16_t name_size = readU16(archive.data() + offset);
        offset += 2;

        if (name_size == 0 || archive.size() - offset < name_size) {
            setError(error_message, "archive entry has invalid name");
            return false;
        }
        const std::string raw_name(reinterpret_cast<const char*>(archive.data() + offset), name_size);
        offset += name_size;
        const std::string name = normalizeEntryName(raw_name);
        if (name.empty()) {
            setError(error_message, "archive entry path is unsafe: " + raw_name);
            return false;
        }

        if (archive.size() - offset < 8) {
            setError(error_message, "archive entry is missing data length");
            return false;
        }
        const uint64_t data_size = readU64(archive.data() + offset);
        offset += 8;
        if (data_size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
            archive.size() - offset < static_cast<size_t>(data_size)) {
            setError(error_message, "archive entry has invalid data length");
            return false;
        }
        if (files.find(name) != files.end()) {
            setError(error_message, "archive contains duplicate entry: " + name);
            return false;
        }

        const unsigned char* begin = archive.data() + offset;
        files[name] = std::vector<unsigned char>(begin, begin + static_cast<size_t>(data_size));
        offset += static_cast<size_t>(data_size);
    }

    if (offset != archive.size()) {
        setError(error_message, "archive contains trailing bytes");
        return false;
    }
    return true;
}

bool verifyManifest(Package& package, std::string* error_message) {
    const std::vector<unsigned char>* manifest_bytes = findFile(package.files, "manifest.json");
    if (manifest_bytes == nullptr) {
        setError(error_message, "manifest.json is missing from package");
        return false;
    }

    try {
        const std::string manifest_text(reinterpret_cast<const char*>(manifest_bytes->data()), manifest_bytes->size());
        package.manifest = nlohmann::json::parse(manifest_text);
    } catch (const std::exception& error) {
        setError(error_message, std::string("manifest.json parse failed: ") + error.what());
        return false;
    }

    if (!package.manifest.contains("format") ||
        !package.manifest["format"].is_string() ||
        package.manifest["format"].get<std::string>() != kManifestFormat) {
        setError(error_message, "manifest.json has unsupported format");
        return false;
    }
    if (!package.manifest.contains("root_config") || !package.manifest["root_config"].is_string()) {
        setError(error_message, "manifest.json missing root_config");
        return false;
    }
    if (!package.manifest.contains("files") || !package.manifest["files"].is_array()) {
        setError(error_message, "manifest.json missing files array");
        return false;
    }

    package.root_config_name = normalizeEntryName(package.manifest["root_config"].get<std::string>());
    if (package.root_config_name.empty()) {
        setError(error_message, "manifest.json root_config path is unsafe");
        return false;
    }

    for (const auto& entry : package.manifest["files"]) {
        if (!entry.is_object() ||
            !entry.contains("path") ||
            !entry.contains("size") ||
            !entry.contains("sha256") ||
            !entry["path"].is_string() ||
            !entry["size"].is_number_unsigned() ||
            !entry["sha256"].is_string()) {
            setError(error_message, "manifest.json contains invalid file entry");
            return false;
        }

        const std::string name = normalizeEntryName(entry["path"].get<std::string>());
        const uint64_t expected_size = entry["size"].get<uint64_t>();
        const std::string expected_sha256 = entry["sha256"].get<std::string>();
        const std::vector<unsigned char>* data = findFile(package.files, name);
        if (data == nullptr) {
            setError(error_message, "manifest references missing file: " + name);
            return false;
        }
        if (static_cast<uint64_t>(data->size()) != expected_size) {
            setError(error_message, "manifest size mismatch for file: " + name);
            return false;
        }
        if (sha256Hex(*data) != expected_sha256) {
            setError(error_message, "manifest sha256 mismatch for file: " + name);
            return false;
        }
    }

    const std::vector<unsigned char>* root_config_bytes = findFile(package.files, package.root_config_name);
    if (root_config_bytes == nullptr) {
        setError(error_message, "root config is missing from package: " + package.root_config_name);
        return false;
    }

    try {
        const std::string root_config_text(reinterpret_cast<const char*>(root_config_bytes->data()),
                                           root_config_bytes->size());
        package.root_config = nlohmann::json::parse(root_config_text);
    } catch (const std::exception& error) {
        setError(error_message, std::string("root config parse failed: ") + error.what());
        return false;
    }

    return true;
}

}  // namespace

bool isEncryptedPackage(const std::string& package_path) {
    return fileHasMagic(package_path, kMagic, sizeof(kMagic));
}

bool isNoKeyPackage(const std::string& package_path) {
    return fileHasMagic(package_path, kNoKeyMagic, sizeof(kNoKeyMagic));
}

bool isPackage(const std::string& package_path) {
    return isEncryptedPackage(package_path) || isNoKeyPackage(package_path);
}

bool loadEncryptedPackage(const std::string& package_path,
                          Package& package,
                          std::string* error_message) {
    std::vector<unsigned char> package_bytes;
    if (!readFile(package_path, package_bytes)) {
        setError(error_message, "failed to read package: " + package_path);
        return false;
    }
    if (package_bytes.size() < kHeaderSize) {
        setError(error_message, "model package is too small");
        return false;
    }
    if (std::memcmp(package_bytes.data(), kMagic, sizeof(kMagic)) != 0) {
        setError(error_message, "model package magic mismatch");
        return false;
    }

    const uint32_t version = readU32(package_bytes.data() + 8);
    const uint32_t flags = readU32(package_bytes.data() + 12);
    const uint64_t ciphertext_size = readU64(package_bytes.data() + 16);
    if (version != kVersion) {
        setError(error_message, "unsupported model package version");
        return false;
    }
    if (flags != kFlags) {
        setError(error_message, "unsupported model package flags");
        return false;
    }
    if (ciphertext_size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
        package_bytes.size() != kHeaderSize + static_cast<size_t>(ciphertext_size)) {
        setError(error_message, "model package ciphertext length mismatch");
        return false;
    }

    const unsigned char* nonce = package_bytes.data() + kAuthenticatedHeaderSize;
    const unsigned char* tag = nonce + kNonceSize;
    const unsigned char* ciphertext_begin = tag + kTagSize;
    const std::vector<unsigned char> ciphertext(ciphertext_begin,
                                                ciphertext_begin + static_cast<size_t>(ciphertext_size));

    std::vector<unsigned char> plaintext;
    if (!aes256GcmDecrypt(ciphertext,
                          nonce,
                          tag,
                          package_bytes.data(),
                          kAuthenticatedHeaderSize,
                          plaintext,
                          error_message)) {
        return false;
    }

    if (!parseArchive(plaintext, package.files, error_message)) {
        std::fill(plaintext.begin(), plaintext.end(), 0);
        return false;
    }
    std::fill(plaintext.begin(), plaintext.end(), 0);

    return verifyManifest(package, error_message);
}

bool loadNoKeyPackage(const std::string& package_path,
                      Package& package,
                      std::string* error_message) {
    std::vector<unsigned char> package_bytes;
    if (!readFile(package_path, package_bytes)) {
        setError(error_message, "failed to read no-key package: " + package_path);
        return false;
    }
    if (package_bytes.size() < kNoKeyHeaderSize) {
        setError(error_message, "no-key model package is too small");
        return false;
    }
    if (std::memcmp(package_bytes.data(), kNoKeyMagic, sizeof(kNoKeyMagic)) != 0) {
        setError(error_message, "no-key model package magic mismatch");
        return false;
    }

    const uint32_t version = readU32(package_bytes.data() + 8);
    const uint32_t flags = readU32(package_bytes.data() + 12);
    const uint64_t archive_size = readU64(package_bytes.data() + 16);
    if (version != kVersion) {
        setError(error_message, "unsupported no-key model package version");
        return false;
    }
    if (flags != kFlags) {
        setError(error_message, "unsupported no-key model package flags");
        return false;
    }
    if (archive_size > static_cast<uint64_t>(std::numeric_limits<size_t>::max()) ||
        package_bytes.size() != kNoKeyHeaderSize + static_cast<size_t>(archive_size)) {
        setError(error_message, "no-key model package archive length mismatch");
        return false;
    }

    const unsigned char* archive_begin = package_bytes.data() + kNoKeyHeaderSize;
    const std::vector<unsigned char> archive(archive_begin,
                                             archive_begin + static_cast<size_t>(archive_size));
    if (!parseArchive(archive, package.files, error_message)) {
        return false;
    }

    return verifyManifest(package, error_message);
}

bool loadPackage(const std::string& package_path,
                 Package& package,
                 std::string* error_message) {
    if (isEncryptedPackage(package_path)) {
        return loadEncryptedPackage(package_path, package, error_message);
    }
    if (isNoKeyPackage(package_path)) {
        return loadNoKeyPackage(package_path, package, error_message);
    }
    setError(error_message, "unsupported model package magic: " + package_path);
    return false;
}

bool writePackageFilesToDirectory(const Package& package,
                                  const std::string& output_dir,
                                  std::string* error_message) {
    try {
        const std::filesystem::path root = std::filesystem::absolute(output_dir);
        std::filesystem::create_directories(root);
        for (const auto& entry : package.files) {
            const std::string normalized = normalizeEntryName(entry.first);
            if (normalized.empty()) {
                setError(error_message, "package entry path is unsafe: " + entry.first);
                return false;
            }

            const std::filesystem::path output_path = root / std::filesystem::path(normalized);
            const std::filesystem::path parent_path = output_path.parent_path();
            if (!parent_path.empty()) {
                std::filesystem::create_directories(parent_path);
            }

            std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
            if (!output.is_open()) {
                setError(error_message, "failed to write package entry: " + output_path.string());
                return false;
            }
            const auto& bytes = entry.second;
            if (!bytes.empty()) {
                output.write(reinterpret_cast<const char*>(bytes.data()),
                             static_cast<std::streamsize>(bytes.size()));
                if (!output) {
                    setError(error_message, "failed to write package entry bytes: " + output_path.string());
                    return false;
                }
            }
        }
    } catch (const std::exception& error) {
        setError(error_message, std::string("failed to extract model package: ") + error.what());
        return false;
    }
    return true;
}

std::string normalizeEntryName(const std::string& entry_name) {
    std::string normalized = entry_name;
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    while (normalized.size() >= 2 && normalized[0] == '.' && normalized[1] == '/') {
        normalized.erase(0, 2);
    }

    if (normalized.empty() ||
        normalized[0] == '/' ||
        normalized.find(':') != std::string::npos) {
        return {};
    }

    size_t start = 0;
    while (start <= normalized.size()) {
        const size_t end = normalized.find('/', start);
        const std::string part = normalized.substr(start, end == std::string::npos ? std::string::npos : end - start);
        if (part.empty() || part == "." || part == "..") {
            return {};
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }

    return normalized;
}

const std::vector<unsigned char>* findFile(const FileMap& files,
                                           const std::string& entry_name) {
    const std::string normalized = normalizeEntryName(entry_name);
    if (!normalized.empty()) {
        const auto iter = files.find(normalized);
        if (iter != files.end()) {
            return &iter->second;
        }
    }

    const std::string basename = normalizeEntryName(baseName(entry_name));
    if (!basename.empty()) {
        const auto iter = files.find(basename);
        if (iter != files.end()) {
            return &iter->second;
        }
    }

    return nullptr;
}

void wipePackageFiles(Package* package) {
    if (package == nullptr) {
        return;
    }
    for (auto& entry : package->files) {
        std::fill(entry.second.begin(), entry.second.end(), 0);
    }
    package->files.clear();
}

}  // namespace algorithm::cdky::model_package
