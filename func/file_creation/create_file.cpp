#include "create_file.h"
#include "../../utils/bmp/reader/bmp_reader.h"
#include "../../utils/json/json.h"
#include "../../utils/logger/logger.h"

void create_file(const std::string& bmp_file_path, const std::string& save_path) {
    // Read the metadata JSON file to get the necessary data
    Data data = read_json(save_path + "/" + std::filesystem::path(bmp_file_path).filename().stem().string() + ".mtd");
    
    // Extract the binary length and original filename
    uint_fast64_t binary_length = data.binary_length;
    std::string original_filename = data.original_filename;
    
    std::cout << "Original Filename: " << original_filename << std::endl;
    std::cout << "Binary Length: " << binary_length << std::endl;

    // Retrieve the encryption key from the Data structure
    std::string encryptionKey = data.encryption_key; // Get the encryption key from the read JSON data

    // Read and reconstruct the BMP file using the encryption key
    readBMP(bmp_file_path, save_path + "/" + original_filename, binary_length, encryptionKey);

    std::cout << "\nOriginal file reconstructed successfully." << std::endl;
}
