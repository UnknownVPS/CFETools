#include <fstream>
#include <string>

struct Data {
    unsigned long long int binary_length;
    std::string original_filename;
};

void write_json(const std::string& path, const Data& data) {
    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Could not open file for writing");
    }
    file << "{\n";
    file << "  \"binary_length\": " << data.binary_length << ",\n";
    file << "  \"original_filename\": \"" << data.original_filename << "\"\n";
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
            data.binary_length = std::stoi(line.substr(line.find(":") + 2, line.rfind(",") - line.find(":") - 2)); // Use std::stoi to convert string to int
        } else if (line.find("original_filename") != std::string::npos) {
            data.original_filename = line.substr(line.find(":") + 3, line.rfind("\"") - line.find(":") - 3);
        }
    }
    file.close();
    return data;
}