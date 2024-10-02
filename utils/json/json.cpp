#include "json.h"

void write_json(const std::string& path, const Data& data) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Could not open file for writing");
    }
    file << "{\n";
    file << "  \"binary_length\": " << data.binary_length << ",\n";
    file << "  \"original_filename\": \"" << data.original_filename << "\",\n";
    file << "  \"encryption_key\": \"" << data.encryption_key << "\"\n"; // Write the encryption key
    file << "}\n";
    file.close();
}

Data read_json(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("Could not open file for reading");
    }
    std::string line;
    Data data;
    while (std::getline(file, line)) {
        if (line.find("binary_length") != std::string::npos) {
            data.binary_length = std::stoull(line.substr(line.find(":") + 2, line.rfind(",") - line.find(":") - 2));
        } else if (line.find("original_filename") != std::string::npos) {
            data.original_filename = line.substr(line.find(":") + 3, line.rfind("\"") - line.find(":") - 3);
        } else if (line.find("encryption_key") != std::string::npos) {
            data.encryption_key = line.substr(line.find(":") + 3, line.rfind("\"") - line.find(":") - 3);
        }
    }
    file.close();
    return data;
}