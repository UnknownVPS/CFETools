#include "create_file.h"
#include "../../utils/bmp/reader/bmp_reader.h"
#include "../../utils/json/json.h"
#include "../../utils/logger/logger.h"
#include "../folder_packer/folder_packer.h"

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
    std::string reconstructedFilePath = save_path + "/" + original_filename;

    // Read and reconstruct the BMP file using the encryption key
    readBMP(bmp_file_path, reconstructedFilePath, binary_length / 8, encryptionKey);

    Logger::Log(LOG_INFO, "Original file reconstructed successfully.");
    // Check if the reconstructed file has ".cfup" extension
    if (original_filename.size() > 5 && original_filename.compare(original_filename.size() - 5, 5, ".cfup") == 0) {
        Logger::Log(LOG_INFO, "Detected .cfup file, starting unpacking...");

        std::string unpackedFolder = (fs::path(save_path) / fs::path(original_filename).stem()).string();

        if (!unpack_packed_file(reconstructedFilePath, unpackedFolder)) {
            Logger::Log(LOG_ERROR, "Failed to unpack the .cfup file: " + reconstructedFilePath);
        } else {
            Logger::Log(LOG_INFO, "Unpacking completed successfully at: " + unpackedFolder);
        }
    }
}
