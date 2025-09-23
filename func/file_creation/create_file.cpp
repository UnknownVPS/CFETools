#include "create_file.h"
#include "../../utils/bmp/reader/bmp_reader.h"
#include "../../utils/json/json.h"
#include "../../utils/logger/logger.h"
#include "../../utils/compress/decompress.h"
#include "../folder_packer/folder_packer.h"
#include "../../utils/userinput/user_input.h"
#include "../../utils/aio/aio_header.h"
#include "../../globals.h"
#include "sodium.h"

void folder_handle (const std::string& save_path, const std::string& reconstructedFilePath, const std::string& original_filename) {
    Logger::Log(LOG_INFO, "Detected .cfup file, starting unpacking...");
    Logger::Log(LOG_DEBUG, save_path + " " + reconstructedFilePath + " " + original_filename);
    std::string unpackedFolder = (std::filesystem::path(save_path) / std::filesystem::path(original_filename).stem()).string();

    if (!unpack_packed_file_toc(reconstructedFilePath, unpackedFolder)) {
        Logger::Log(LOG_ERROR, "Failed to unpack the .cfup file: " + reconstructedFilePath);
    } else {
        Logger::Log(LOG_INFO, "Unpacking completed successfully at: " + unpackedFolder);
    }
}

void compress_handle(const std::string& save_path, const std::string& reconstructedFilePath) {
    Logger::Log(LOG_INFO, "Detected .cfmp file, starting decompression...");
    std::string decompressedFilePath = reconstructedFilePath.substr(0, reconstructedFilePath.size() - 5);
    if (decompressFile(reconstructedFilePath, decompressedFilePath)) {
        Logger::Log(LOG_INFO, "Decompression completed: " + decompressedFilePath);
    } else {
        Logger::Log(LOG_ERROR, "Decompression failed for: " + reconstructedFilePath);
    }
    if (decompressedFilePath.size() > 5 && 
        decompressedFilePath.compare(decompressedFilePath.size() - 5, 5, ".cfup") == 0) {
        Logger::Log(LOG_DEBUG, save_path + " " + decompressedFilePath + " " + std::filesystem::path(decompressedFilePath).filename().string());
        folder_handle(save_path, decompressedFilePath, std::filesystem::path(decompressedFilePath).filename().string());
    }
}

static std::string deriveKeyBlake2b(const std::string& password) {
    unsigned char out[32];
    crypto_generichash(out, sizeof out,
                       reinterpret_cast<const unsigned char*>(password.data()),
                       password.size(),
                       nullptr, 0);
    return std::string(reinterpret_cast<char*>(out), sizeof out);
}

bool compressed;
bool packed;
bool readAIOHeaderFromBMP(const std::string& bmp_file_path, std::string& original_filename, 
                          uint64_t& extracted_length, bool& is_encrypted) {
    std::ifstream file(bmp_file_path, std::ios::binary);
    if (!file) {
        Logger::Log(LOG_ERROR, "Failed to open BMP file: " + bmp_file_path);
        return false;
    }

    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;
    
    // Read BMP headers
    file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
    file.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));
    
    if (fileHeader.file_type != 0x4D42) {
        Logger::Log(LOG_ERROR, "Invalid BMP file format");
        return false;
    }

    // Skip color table based on bit depth
    if (infoHeader.bit_count == 8) {
        // 8-bit grayscale has 256 color entries (1024 bytes)
        file.seekg(sizeof(unsigned int) * 256, std::ios::cur);
    } else if (infoHeader.bit_count == 1) {
        // 1-bit monochrome has 2 color entries (8 bytes)
        file.seekg(sizeof(unsigned int) * 2, std::ios::cur);
    } else {
        Logger::Log(LOG_ERROR, "Unsupported bit depth: " + std::to_string(infoHeader.bit_count));
        return false;
    }

    // Try to read AIO header
    AIOHeaderReader aioReader;
    if (!aioReader.readFromStream(file)) {
        Logger::Log(LOG_DEBUG, "No valid AIO header found in BMP file");
        return false;
    }

    // Extract values from AIO header
    uint64_t binary_length_bits;
    if (!aioReader.getUInt64("binary_length", binary_length_bits)) {
        Logger::Log(LOG_ERROR, "Failed to read binary_length from AIO header");
        return false;
    }
    
    if (!aioReader.getString("filename", original_filename)) {
        Logger::Log(LOG_ERROR, "Failed to read filename from AIO header");
        return false;
    }
    
    if (!aioReader.getBool("encrypted", is_encrypted)) {
        Logger::Log(LOG_ERROR, "Failed to read encryption flag from AIO header");
        return false;
    }

    // Convert bits to bytes
    extracted_length = binary_length_bits / 8;

    std::string version;

    aioReader.getString("v", version);
    aioReader.getBool("compress", compressed);
    aioReader.getBool("pack", packed);
    // Validation checks
    if (original_filename.empty() || original_filename.length() > 512) {
        Logger::Log(LOG_ERROR, "Invalid filename in AIO header");
        return false;
    }
    
    if (extracted_length == 0 || extracted_length > (1ULL << 40)) {
        Logger::Log(LOG_ERROR, "Invalid binary length in AIO header");
        return false;
    }

    Logger::Log(LOG_INFO, "AIO header successfully read from BMP file");
    Logger::Log(LOG_DEBUG, "Original Filename: " + original_filename);
    Logger::Log(LOG_DEBUG, "Extracted Length: " + std::to_string(extracted_length) + " bytes");
    Logger::Log(LOG_DEBUG, "Encryption: " + std::string(is_encrypted ? "Yes" : "No"));
    Logger::Log(LOG_DEBUG, "Bit Depth: " + std::to_string(infoHeader.bit_count) + "-bit");
    Logger::Log(LOG_DEBUG, "Version used: " + version);
    Logger::Log(LOG_DEBUG, "Compressed: " + std::string(compressed ? "Yes" : "No"));
    Logger::Log(LOG_DEBUG, "Packed: " + std::string(packed ? "Yes" : "No"));

    return true;
}

void create_file(const std::string& bmp_file_path) {
    std::string original_filename;
    std::string reconstructedFilePath;
    uint64_t extracted_length = 0;
    bool is_encrypted = false;
    // Try to extract using new AIO header system
    if (readAIOHeaderFromBMP(bmp_file_path, original_filename, extracted_length, is_encrypted)) {
        reconstructedFilePath = save_path + "/" + original_filename;

        std::string encryptionKey;
        if (is_encrypted) {
            Logger::Log(LOG_INFO, "This file is encrypted.");
            Input inputprompt;
            std::string password = inputprompt.ask("Please enter a password: ");
            
            // Derive a 32-byte key with BLAKE2b
            encryptionKey = deriveKeyBlake2b(password);

            // Best-effort wipe of plaintext password from memory
            if (!password.empty()) {
                sodium_memzero(password.data(), password.size());
            }
        }

        Logger::Log(LOG_INFO, "AIO header detected in BMP. Reconstructing file: " + original_filename);
        
        readBMP(bmp_file_path, reconstructedFilePath, extracted_length, encryptionKey);

        Logger::Log(LOG_INFO, "Original file reconstructed successfully.");
        
        // Handle special file types
        if (packed) {
            folder_handle(save_path, reconstructedFilePath, original_filename);
        }
        if (compressed) {
            compress_handle(save_path, reconstructedFilePath);
        }
        return;
    }

    // Fallback to legacy .mtd file system
    Logger::Log(LOG_INFO, "No AIO header found, falling back to .mtd metadata file");
    
    std::filesystem::path bmp_path(bmp_file_path);
    std::string metadata_file_path = bmp_path.parent_path().string() + "/" + bmp_path.stem().string() + ".mtd";

    if (!std::filesystem::exists(metadata_file_path)) {
        Logger::Log(LOG_ERROR, "Error: Metadata file (.mtd) not found for the given file and no AIO header detected.");
        return;
    }

    Data data = read_json(metadata_file_path);

    uint_fast64_t binary_length = data.binary_length;
    original_filename = data.original_filename;
    reconstructedFilePath = save_path + "/" + original_filename;

    Logger::Log(LOG_DEBUG, "Original Filename: " + original_filename);
    Logger::Log(LOG_DEBUG, "Binary Length: " + std::to_string(binary_length));

    std::string encryptionKey = data.encryption_key;
    readBMP(bmp_file_path, reconstructedFilePath, binary_length / 8, encryptionKey);

    Logger::Log(LOG_INFO, "Original file reconstructed successfully.");
    
    // Handle special file types for legacy system
    if (original_filename.size() > 5 && original_filename.compare(original_filename.size() - 5, 5, ".cfmp") == 0) {
        compress_handle(save_path, reconstructedFilePath);
    }
    if (original_filename.size() > 5 && original_filename.compare(original_filename.size() - 5, 5, ".cfup") == 0) {
        folder_handle(save_path, reconstructedFilePath, original_filename);
    }
}
