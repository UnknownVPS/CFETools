#include "create_img.h"
#include "../../utils/bmp/writer/bmp_writer.h"  // Include the BMP writer header file
#include "../../utils/json/json.h"
#include "../../utils/logger/logger.h"

std::string generateRandomKey(size_t length) {
    std::random_device rd;  // Random number generator
    std::uniform_int_distribution<int> dist(0, 255); // Distribution for byte values
    std::ostringstream oss;

    for (size_t i = 0; i < length; ++i) {
        int byte = dist(rd); // Generate random byte
        oss << std::hex << std::setw(2) << std::setfill('0') << byte; // Convert to hex
    }
    
    return oss.str();
}

void create_img(const std::string& file_path, const std::string& save_path) {
    std::string file_name = std::filesystem::path(file_path).filename().stem().string();
    Logger::Log(LOG_DEBUG, "Opening file for reading");
    std::ifstream inputFile(file_path, std::ios::binary);
    inputFile.seekg(0, std::ios::end);
    std::uintmax_t size = inputFile.tellg(); 
    inputFile.seekg(0, std::ios::beg);
    
    Data data;
    data.original_filename = std::filesystem::path(file_path).filename().string();
    data.binary_length = (size * 8);
    
    // Generate random encryption key
    std::string encryptionKey = generateRandomKey(32); // Generate a 256-bit key (32 bytes)
    data.encryption_key = encryptionKey; // Assuming you added this field in the Data struct

    Logger::Log(LOG_DEBUG, "Writing JSON file.");
    write_json(save_path + '/' + file_name + ".mtd", data);
    
    // Call the writeBMP function
    writeBMP(save_path + "/" + file_name + ".bmp", file_path, encryptionKey);
    Logger::Log(LOG_INFO, "Image written successfully!");
}