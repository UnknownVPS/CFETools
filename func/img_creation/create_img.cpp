#include "create_img.h"
#include "../../utils/bmp/writer/bmp_writer.h"
#include "../../utils/json/json.h"
#include "../../utils/logger/logger.h"
#include "../../utils/userinput/user_input.h"
#include "../../globals.h"
#include "../../version.h"
#include "../../utils/hashers/fileHasher.hpp"
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

// Generate 32-byte key and return as hex string
std::string generateRandomKey() {
    unsigned char key[crypto_aead_xchacha20poly1305_ietf_KEYBYTES]; // 32 bytes
    crypto_aead_xchacha20poly1305_ietf_keygen(key);

    // Hex encode
    char hex[crypto_aead_xchacha20poly1305_ietf_KEYBYTES * 2 + 1];
    sodium_bin2hex(hex, sizeof hex, key, sizeof key);

    return std::string(hex);
}

void create_img(const std::string& file_path) {
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

    json_utils::JsonMap data;
    data["original_filename"] = std::filesystem::path(file_path).filename().string();
    data["binary_length"] = std::to_string(size * 8);
    
    std::string encryptionKey;
    if (no_encrypt) {
        encryptionKey.clear();
    } else {
        // Default (AIO): random 32-byte key written to .mtd for compatibility
        encryptionKey = generateRandomKey();
    }
    data["encryption_key"] = encryptionKey;
    data["v"] = VERSION;
    data["compress"] = isCompressed ? "true" : "false";
    data["pack"] = isPacked ? "true" : "false";
    if (!disableHash && twofile_system) {
        Logger::StartTimer("xxHash calculation");
        std::string hash = fileHasher::xxhash_file(file_path);
        Logger::EndTimer("xxHash calculation", LOG_INFO);
        data["hash"] = hash;
    } else {
        data["hash"] = "";
    }
    if (twofile_system) {
        if (shaEnabled) {
            std::string sha = fileHasher::hashFileSHA256(file_path);
            data["SHA"] = sha;
        }
        if (crcEnabled) {
            std::string crc = fileHasher::crc32_file(file_path);
            data["CRC"] = crc;
        }
        Logger::Log(LOG_DEBUG, "Writing JSON file.");
        json_utils::write_json(save_path + '/' + file_name + ".mtd", data);
    }

    if (!twofile_system && !no_encrypt) {
        Logger::Log(LOG_INFO, "AIO Mode with Encryption requires a password");
        std::string password;
        Input inputprompt;
        password = inputprompt.ask("Please enter a password (be aware not recoverable): ");

        // Derive a 32-byte key with BLAKE2b
        encryptionKey = deriveKeyBlake2b(password);

        // Best-effort wipe of plaintext password from memory
        if (!password.empty()) {
            sodium_memzero(password.data(), password.size());
        }
    }

    writeBMP(save_path + "/" + file_name + ".bmp", file_path, encryptionKey);
    Logger::Log(LOG_INFO, "Image written successfully!");
}
