#include "create_img.h"
#include "../../utils/bmp/writer/bmp_writer.h"
#include "../../utils/json/json.h"
#include "../../utils/logger/logger.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <sodium.h>

static std::string deriveKeyBlake2b(const std::string& password) {
    unsigned char out[32];
    crypto_generichash(out, sizeof out,
                       reinterpret_cast<const unsigned char*>(password.data()),
                       password.size(),
                       nullptr, 0);
    return std::string(reinterpret_cast<char*>(out), sizeof out);
}

std::string generateRandomKey(size_t length) {
    std::vector<uint8_t> key(length);
    randombytes_buf(key.data(), length);
    return std::string(key.begin(), key.end());
}

void create_img(const std::string& file_path, const std::string& save_path,
                bool no_encrypt, bool aio_mode, bool grayscaleMode) {
    if (sodium_init() < 0) {
        Logger::Log(LOG_ERROR, "libsodium initialization failed");
        return;
    }

    std::string file_name = std::filesystem::path(file_path).filename().stem().string();
    Logger::Log(LOG_DEBUG, "Opening file for reading");
    std::ifstream inputFile(file_path, std::ios::binary);
    inputFile.seekg(0, std::ios::end);
    std::uintmax_t size = inputFile.tellg();
    inputFile.seekg(0, std::ios::beg);

    Data data;
    data.original_filename = std::filesystem::path(file_path).filename().string();
    data.binary_length = (size * 8);

    std::string encryptionKey;
    if (no_encrypt) {
        encryptionKey.clear();
    } else {
        // Default (non-AIO): random 32-byte key written to .mtd for compatibility
        encryptionKey = generateRandomKey(32);
    }
    data.encryption_key = encryptionKey;

    if (!aio_mode) {
        Logger::Log(LOG_DEBUG, "Writing JSON file.");
        write_json(save_path + '/' + file_name + ".mtd", data);
    }

    if (aio_mode && !no_encrypt) {
        Logger::Log(LOG_INFO, "AIO Mode with Encryption requires a password");
        std::string password;
        std::cout << "Enter password: ";
        std::getline(std::cin, password);

        // Derive a 32-byte key with BLAKE2b
        encryptionKey = deriveKeyBlake2b(password);

        // Best-effort wipe of plaintext password from memory
        if (!password.empty()) {
            sodium_memzero(password.data(), password.size());
        }
    }

    writeBMP(save_path + "/" + file_name + ".bmp", file_path, encryptionKey, no_encrypt, grayscaleMode, aio_mode);
    Logger::Log(LOG_INFO, "Image written successfully!");
}
