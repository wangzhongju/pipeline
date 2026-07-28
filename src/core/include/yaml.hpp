#ifndef __YAML_HPP__
#define __YAML_HPP__

#include <yaml-cpp/yaml.h>

#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class YAMLHelper {
   private:
    YAML::Node _root;
    YAMLHelper() {}
    bool _load(const char* filename) {
        try {
            _root = YAML::LoadFile(filename);
            return true;
        } catch (...) {
            return false;
        }
    }

   public:
    /**
     * Create an instance.
     *
     * @param filename YAML config file name.
     * @return Return object or null.
     */
    static std::unique_ptr<YAMLHelper> Create(const char* filename) {
        YAMLHelper* obj = new YAMLHelper();
        if (obj && obj->_load(filename)) {
            return std::unique_ptr<YAMLHelper>(obj);
        } else {
            delete obj;
            return nullptr;
        }
    }

    /**
     * Common get.
     *
     * @param key_path Key path. such as "key1.subkey"
     * @return std::optional<T>
     */
    template <typename T>
    std::optional<T> get(const std::string& key_path) const {
        std::istringstream ss(key_path);
        std::string token;
        YAML::Node node = _root;
        while (std::getline(ss, token, '.')) {
            if (!node || !node.IsMap() || !node[token]) {
                return std::nullopt;
            }
            node.reset(node[token]);
        }
        try {
            return node.as<T>();
        } catch (...) {
            return std::nullopt;
        }
    }
    /**
     * Common get.
     *
     * @param key_path Key path. such as "key1.subkey"
     * @param default_value Default value, return this value if the key doesn't exist.
     * @return T
     */
    template <typename T>
    T get(const std::string& key_path, const T& default_value) const {
        auto t = get<T>(key_path);
        if (t) {
            return *t;
        } else {
            return default_value;
        }
    }
    /**
     * Common set.
     *
     * @param key_path Key path. such as "key1.subkey"
     * @param value Value to set.
     */
    template <typename T>
    void set(const std::string& key_path, const T& value) {
        std::istringstream ss(key_path);
        std::string token;
        YAML::Node* node = &_root;
        while (std::getline(ss, token, '.')) {
            if (!*node) {
                return;
            }
            YAML::Node child = (*node)[token];
            node = &child;
        }
        *node = value;
    }
};

#endif  //__YAML_HPP__
