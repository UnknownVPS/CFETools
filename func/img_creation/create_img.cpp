#include "create_img.h"
#include "../../utils/bmp/writer/bmp_writer.h"
#include "../../utils/bmp/writer/bmp_writer_noenc.h"
#include "../../utils/json/json.h"
#include "../../utils/logger/logger.h"
#include <random>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <fstream>
#include <sodium.h>

std::string generateRandomKey(size_t length) {
    std::vector<uint8_t> key(length);
    randombytes_buf(key.data(), length);
    return std::string(key.begin(), key.end());
}

void create_img(const std::string& file_path, const std::string& save_path, bool no_encrypt, bool aio_mode) {
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
        encryptionKey = "";
    } else {
        encryptionKey = generateRandomKey(32); // 256-bit key
    }
    data.encryption_key = encryptionKey;

    if (!aio_mode) {
        Logger::Log(LOG_DEBUG, "Writing JSON file.");
        write_json(save_path + '/' + file_name + ".mtd", data);
    }

    if (no_encrypt || aio_mode) {
        writeBMPNoEncrypt(save_path + "/" + file_name + ".bmp", file_path, aio_mode);
    } else {
        writeBMP(save_path + "/" + file_name + ".bmp", file_path, encryptionKey);
    }
    Logger::Log(LOG_INFO, "Image written successfully!");
}