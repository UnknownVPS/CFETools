#include "create_file.h"
#include "../../utils/bmp/reader/bmp_reader.h"
#include "../../utils/json/json.h"
#include "../../utils/logger/logger.h"

void create_file(const std::string& bmp_file_path, const std::string& save_path) {
    // Get the directory and stem (filename without extension) of the BMP file
    std::filesystem::path bmp_path(bmp_file_path);
    std::string metadata_file_path = bmp_path.parent_path().string() + "/" + bmp_path.stem().string() + ".mtd";

    // Check if the .mtd file exists in the same directory
    if (!std::filesystem::exists(metadata_file_path)) {
        Logger::Log(LOG_ERROR, "Error: Metadata file (.mtd) not found for the given file.");
        return;
    }

    // Read the metadata JSON file to get the necessary data
    Data data = read_json(metadata_file_path);
    
    // Extract the binary length and original filename
    uint_fast64_t binary_length = data.binary_length;
    std::string original_filename = data.original_filename;
    
    Logger::Log(LOG_DEBUG, "Original Filename: " + original_filename);
    Logger::Log(LOG_DEBUG, "Binary Length: " + std::to_string(binary_length));

    // Retrieve the encryption key from the Data structure
    std::string encryptionKey = data.encryption_key; // Get the encryption key from the read JSON data

    // Read and reconstruct the BMP file using the encryption key
    readBMP(bmp_file_path, save_path + "/" + original_filename, binary_length, encryptionKey);

    Logger::Log(LOG_INFO, "Original file reconstructed successfully.");
}
