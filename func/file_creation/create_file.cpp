#include "create_file.h"
#include "../../utils/bmp/reader/bmp_reader.h"
#include "../../utils/json/json.h"
#include "../../utils/logger/logger.h"
#include "../../utils/compress/decompress.h"
#include "../folder_packer/folder_packer.h"
#include "../../utils/userinput/user_input.h"
#include "sodium.h"

void folder_handle (const std::string& save_path, const std::string& reconstructedFilePath, const std::string& original_filename) {
    Logger::Log(LOG_INFO, "Detected .cfup file, starting unpacking...");
    Logger::Log(LOG_DEBUG, save_path + " " + reconstructedFilePath + " " + original_filename);
    std::string unpackedFolder = (std::filesystem::path(save_path) / std::filesystem::path(original_filename).stem()).string();

    if (!unpack_packed_file(reconstructedFilePath, unpackedFolder)) {
        Logger::Log(LOG_ERROR, "Failed to unpack the .cfup file: " + reconstructedFilePath);
    } else {
        Logger::Log(LOG_INFO, "Unpacking completed successfully at: " + unpackedFolder);
    }
}

void compress_handle(const std::string& save_path, const std::string& reconstructedFilePath, const std::string& original_filename) {
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

void create_file(const std::string& bmp_file_path, const std::string& save_path) {
    std::string original_filename;
    std::string reconstructedFilePath;
    uint64_t extracted_length = 0;

    // Try to extract using AIO header (auto-detect)
    {
        std::ifstream file(bmp_file_path, std::ios::binary);
        if (file) {
            BMPFileHeader fileHeader;
            BMPInfoHeader infoHeader;
            file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
            file.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));

            // FIXED: Skip color table based on bit depth
            if (infoHeader.bit_count == 8) {
                // 8-bit grayscale has 256 color entries (1024 bytes)
                file.seekg(sizeof(unsigned int) * 256, std::ios::cur);
            } else if (infoHeader.bit_count == 1) {
                // 1-bit monochrome has 2 color entries (8 bytes)
                file.seekg(sizeof(unsigned int) * 2, std::ios::cur);
            } else {
                Logger::Log(LOG_ERROR, "Unsupported bit depth: " + std::to_string(infoHeader.bit_count));
                return;
            }

            uint64_t aio_bin_len = 0;
            uint16_t aio_fname_len = 0;
            file.read(reinterpret_cast<char*>(&aio_bin_len), sizeof(aio_bin_len));
            file.read(reinterpret_cast<char*>(&aio_fname_len), sizeof(aio_fname_len));
            if (aio_fname_len > 0 && aio_fname_len < 512 && aio_bin_len > 0 && aio_bin_len < (1ULL << 40)) {
                std::string aio_fname(aio_fname_len, '\0');
                file.read(&aio_fname[0], aio_fname_len);
                original_filename = aio_fname;
                extracted_length = aio_bin_len / 8;
                reconstructedFilePath = save_path + "/" + original_filename;

                // Read encryptedFlag (1 = encrypted, 0 = not encrypted)
                uint8_t encryptedFlag = 0;
                file.read(reinterpret_cast<char*>(&encryptedFlag), sizeof(encryptedFlag));

                std::string password;
                std::string encryptionKey;
                if (encryptedFlag == 1) {
                    Logger::Log(LOG_INFO, "This file is encrypted.");
                    Input inputprompt;
                    password = inputprompt.ask("Please enter a password: ");
                    // Derive a 32-byte key with BLAKE2b
                    encryptionKey = deriveKeyBlake2b(password);

                    // Best-effort wipe of plaintext password from memory
                    if (!password.empty()) {
                        sodium_memzero(password.data(), password.size());
                    }
                }

                Logger::Log(LOG_INFO, "AIO header detected in BMP. Reconstructing file: " + original_filename);
                Logger::Log(LOG_DEBUG, "Extracted Length: " + std::to_string(extracted_length));
                Logger::Log(LOG_DEBUG, "Extracted Filename: " + original_filename);
                Logger::Log(LOG_DEBUG, "Encryption Flag: " + std::string(encryptedFlag ? "Encrypted" : "Not Encrypted"));
                Logger::Log(LOG_DEBUG, "Bit Depth: " + std::to_string(infoHeader.bit_count) + "-bit");

                readBMP(bmp_file_path, reconstructedFilePath, extracted_length, encryptionKey);

                Logger::Log(LOG_INFO, "Original file reconstructed successfully.");
                if (original_filename.size() > 5 && original_filename.compare(original_filename.size() - 5, 5, ".cfup") == 0) {
                    folder_handle(save_path, reconstructedFilePath, original_filename);
                }
                return;
            }
        }
    }

    // Fallback to .mtd
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
    std::string decompressedFilePath = "";
    readBMP(bmp_file_path, reconstructedFilePath, binary_length / 8, encryptionKey);

    Logger::Log(LOG_INFO, "Original file reconstructed successfully.");
    if (original_filename.size() > 5 && original_filename.compare(original_filename.size() - 5, 5, ".cfmp") == 0) {
        compress_handle(save_path, reconstructedFilePath, original_filename);
    }
    if (original_filename.size() > 5 && original_filename.compare(original_filename.size() - 5, 5, ".cfup") == 0) {
        folder_handle(save_path, reconstructedFilePath, original_filename);
    }
}