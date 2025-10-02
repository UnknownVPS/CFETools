#include <string>
#include <fstream>
#include <filesystem>
#include <sodium.h>
#include "../../utils/logger/logger.h"
#include "../../utils/aio/aio_header.h"
#include "../../utils/userinput/user_input.h"
#include "../../globals.h"
#include "../../utils/bmp/reader/bmp_reader.h"
#include "../../utils/hashers/fileHasher.hpp"
#include "../folder_packer/folder_packer.h"
#include "../../utils/compress/decompress.h"
#include "../../utils/json/json.h"

static std::string deriveKeyBlake2b(const std::string& password) {
    unsigned char out[32];
    crypto_generichash(out, sizeof out,
                       reinterpret_cast<const unsigned char*>(password.data()),
                       password.size(),
                       nullptr, 0);
    return std::string(reinterpret_cast<char*>(out), sizeof out);
}

bool hash_handle(const std::string& file_path, const std::string& global_hash, const std::string& sha, const std::string& crc) {
    bool result = true;

    // Check xxHash
    if (global_hash.empty() || disableHash) {
        return true; // No checks
    }

    Logger::StartTimer("xxHash calculation");
    std::string hash = fileHasher::xxhash_file(file_path);
    Logger::EndTimer("xxHash calculation", LOG_INFO);

    if (hash == global_hash) {
        Logger::Log(LOG_INFO, "Hash matches the original file hash.");
    } else {
        Logger::Log(LOG_WARNING, "Hash does not match the original file hash.");
        result = false;
    }

    if (!sha.empty() && !shaEnabled) {
        Logger::Log(LOG_INFO, "SHA-256 verification available but disabled. Enable SHA verification to check.");
    }
    if (!crc.empty() && !crcEnabled) {
        Logger::Log(LOG_INFO, "CRC32 verification available but disabled. Enable CRC32 verification to check.");
    }
    // Check SHA-256
    if (!sha.empty() && shaEnabled) {
        Logger::StartTimer("SHA-256 calculation");
        std::string file_sha = fileHasher::hashFileSHA256(file_path);
        Logger::EndTimer("SHA-256 calculation", LOG_INFO);
        if (file_sha == sha) {
            Logger::Log(LOG_INFO, "SHA-256 matches the original file SHA-256.");
        } else {
            Logger::Log(LOG_WARNING, "SHA-256 does not match the original file SHA-256.");
            result = false;
        }
    }

    // Check CRC32
    if (!crc.empty() && crcEnabled) {
        Logger::StartTimer("CRC32 calculation");
        std::string file_crc = fileHasher::crc32_file(file_path);
        Logger::EndTimer("CRC32 calculation", LOG_INFO);
        if (file_crc == crc) {
            Logger::Log(LOG_INFO, "CRC32 matches the original file SHA-256.");
        } else {
            Logger::Log(LOG_WARNING, "CRC32 does not match the original file SHA-256.");
            result = false;
        }
    }
    return result;
}

void folder_handle (const std::string& save_path, const std::string& reconstructedFilePath, const std::string& original_filename) {
    Logger::Log(LOG_INFO, "Detected .cfup file, starting unpacking...");
    Logger::Log(LOG_DEBUG, save_path + " " + reconstructedFilePath + " " + original_filename);
    std::string unpackedFolder = (std::filesystem::path(save_path) / std::filesystem::path(original_filename).stem()).string();

    if (!unpack_packed_file(reconstructedFilePath, unpackedFolder)) {
        Logger::Log(LOG_ERROR, "Failed to unpack the .cfup file: " + reconstructedFilePath);
    } else {
        Logger::Log(LOG_DEBUG, "Removing temporary packed file: " + reconstructedFilePath);
        std::filesystem::remove(reconstructedFilePath);
        Logger::Log(LOG_INFO, "Unpacking completed successfully at: " + unpackedFolder);
    }
}

void compress_handle(const std::string& save_path, const std::string& reconstructedFilePath, bool isPacked) {
    Logger::Log(LOG_INFO, "Detected .cfmp file, starting decompression...");
    std::string decompressedFilePath = reconstructedFilePath.substr(0, reconstructedFilePath.size() - 5);
    if (decompressFile(reconstructedFilePath, decompressedFilePath)) {
        Logger::Log(LOG_DEBUG, "Removing temporary compressed file: " + reconstructedFilePath);
        std::filesystem::remove(reconstructedFilePath);
        Logger::Log(LOG_INFO, "Decompression completed: " + decompressedFilePath);
    } else {
        Logger::Log(LOG_ERROR, "Decompression failed for: " + reconstructedFilePath);
    }
    if (isPacked) {
        Logger::Log(LOG_DEBUG, save_path + " " + decompressedFilePath + " " + std::filesystem::path(decompressedFilePath).filename().string());
        folder_handle(save_path, decompressedFilePath, std::filesystem::path(decompressedFilePath).filename().string());
    }
}

bool createFile(const std::string& bmp_file_path) {
    if (!std::filesystem::exists(bmp_file_path)) {
        Logger::Log(LOG_ERROR, "Error: BMP file does not exist: " + bmp_file_path);
        return false;
    }
    bool twofile_system = false;
    std::string filename, version, global_hash, sha, crc, encryptionKey;
    uint64_t binLength = 0;
    bool compressed, packed, encrypted = 0;
    std::ifstream file(bmp_file_path, std::ios::binary);

    // Read BMP headers
    BMPFileHeader fileHeader;
    BMPInfoHeader infoHeader;
    file.read(reinterpret_cast<char*>(&fileHeader), sizeof(fileHeader));
    file.read(reinterpret_cast<char*>(&infoHeader), sizeof(infoHeader));

    // Skip color table
    if (infoHeader.bit_count == 8) file.seekg(1024, std::ios::cur);
    else if (infoHeader.bit_count == 1) file.seekg(8, std::ios::cur);

    // Now try reading AIO header
    AIOHeaderReader aioReader;
    if (!aioReader.readFromStream(file)) {
        Logger::Log(LOG_INFO, "No valid AIO header found in BMP file, checking for .mtd file.");
        twofile_system = true;
    }
    if (twofile_system) {
        json_utils::JsonMap data = json_utils::read_json(std::filesystem::path(bmp_file_path).replace_extension(".mtd").string());
        if (data.empty()) {
            Logger::Log(LOG_ERROR, "Error: Metadata file (.mtd) not found for the given file and no AIO header detected.");
            return false;
        }
        filename = data["original_filename"];
        binLength = std::stoull(data["binary_length"]);
        global_hash = data["hash"];
        sha = data["SHA"];
        crc = data["CRC"];
        compressed = (data["compress"] == "true");
        packed = (data["pack"] == "true");
        Logger::Log(LOG_DEBUG, "Variables Set: " + filename + " " + std::to_string(binLength) + " " + global_hash + " " + sha + " " + crc + " " + std::to_string(compressed) + " " + std::to_string(packed));
        
        readBMP(bmp_file_path, save_path + "/" + filename, binLength / 8, data["encryption_key"]);
        if (hash_handle(save_path + "/" + filename, global_hash, sha, crc)) {
            Logger::Log(LOG_INFO, "Original file reconstructed successfully.");
        } else {
            Logger::Log(LOG_WARNING, "File reconstruction failed (or) hash mismatch detected.");
            return false;
        }
        if (compressed) {
            compress_handle(save_path, save_path + "/" + filename, packed);
        }
        if (packed && !compressed) {
            folder_handle(save_path, save_path + "/" + filename, filename);
        }
        return true;
    }
    aioReader.getString("filename", filename);
    aioReader.getString("v", version);
    aioReader.getUInt64("binary_length", binLength);
    aioReader.getString("hash", global_hash);
    aioReader.getString("SHA", sha);
    aioReader.getString("CRC", crc);
    aioReader.getBool("compress", compressed);
    aioReader.getBool("pack", packed);
    aioReader.getBool("encrypted", encrypted);
    Logger::Log(LOG_DEBUG, "Variables Set: " + filename + " " + version + " " + std::to_string(binLength) + " " + global_hash + " " + sha + " " + crc + " " + std::to_string(compressed) + " " + std::to_string(packed));
        
    if (encrypted) {
        Logger::Log(LOG_INFO, "Using AIO Mode with Encryption. Please enter the password used during creation.");
        std::string password;
        Input inputprompt;
        password = inputprompt.ask("Please enter a password: ");
        encryptionKey = deriveKeyBlake2b(password);
        
        if (!password.empty()) {
            sodium_memzero(password.data(), password.size());
        }
    }
    readBMP(bmp_file_path, save_path + "/" + filename, binLength / 8, encryptionKey);
    if (hash_handle(save_path + "/" + filename, global_hash, sha, crc)) {
        Logger::Log(LOG_INFO, "Original file reconstructed successfully.");
    } else {
        Logger::Log(LOG_WARNING, "File reconstruction failed (or) hash mismatch detected.");
        return false;
    }
    if (compressed) {
        compress_handle(save_path, save_path + "/" + filename, packed);
    }
    if (packed && !compressed) {
        folder_handle(save_path, save_path + "/" + filename, filename);
    }
    return true;
}