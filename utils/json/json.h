#pragma once
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unordered_map>

namespace json_utils {

    using JsonMap = std::unordered_map<std::string, std::string>;

    inline void write_json(const std::string& path, const JsonMap& data) {
        std::ofstream file(path);
        if (!file) {
            throw std::runtime_error("Could not open file for writing");
        }
        file << "{\n";
        size_t count = 0;
        for (const auto& [key, value] : data) {
            file << "  \"" << key << "\": \"" << value << "\"";
            if (++count < data.size()) file << ",";
            file << "\n";
        }
        file << "}\n";
    }

    inline JsonMap read_json(const std::string& path) {
        std::ifstream file(path);
        if (!file) {
            throw std::runtime_error("Could not open file for reading");
        }

        JsonMap data;
        std::string line;
        while (std::getline(file, line)) {
            auto key_start = line.find("\"");
            if (key_start == std::string::npos) continue;

            auto key_end = line.find("\"", key_start + 1);
            std::string key = line.substr(key_start + 1, key_end - key_start - 1);

            auto value_start = line.find("\"", key_end + 1);
            auto value_end = line.find("\"", value_start + 1);
            std::string value = line.substr(value_start + 1, value_end - value_start - 1);

            data[key] = value;
        }
        return data;
    }

}