#include "create_file.h"
#include "../../utils/bmp/reader/bmp_reader.h"
#include "../../utils/bmp/reader/bmp_reader_noenc.h"
#include "../../utils/json/json.h"
#include "../../utils/logger/logger.h"
#include "../folder_packer/folder_packer.h"

void create_file(const std::string& bmp_file_path, const std::string& save_path, bool /*aio_mode*/) {
    bool extracted = false;
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
            file.seekg(sizeof(unsigned int) * 2, std::ios::cur); // skip color table

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
                extracted = true;
            }
        }
    }

    if (extracted) {
        Logger::Log(LOG_INFO, "AIO header detected in BMP. Reconstructing file: " + original_filename);
        Logger::Log(LOG_DEBUG, "Extracted Length: " + std::to_string(extracted_length));
        Logger::Log(LOG_DEBUG, "Extracted Filename: " + original_filename);
        readBMPNoEncrypt(bmp_file_path, reconstructedFilePath, extracted_length);

        Logger::Log(LOG_INFO, "Original file reconstructed successfully.");
        if (original_filename.size() > 5 && original_filename.compare(original_filename.size() - 5, 5, ".cfup") == 0) {
            Logger::Log(LOG_INFO, "Detected .cfup file, starting unpacking...");
            std::string unpackedFolder = (std::filesystem::path(save_path) / std::filesystem::path(original_filename).stem()).string();

            if (!unpack_packed_file(reconstructedFilePath, unpackedFolder)) {
                Logger::Log(LOG_ERROR, "Failed to unpack the .cfup file: " + reconstructedFilePath);
            } else {
                Logger::Log(LOG_INFO, "Unpacking completed successfully at: " + unpackedFolder);
            }
        }
        return;
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

    if (encryptionKey.empty()) {
        readBMPNoEncrypt(bmp_file_path, reconstructedFilePath, binary_length / 8);
    } else {
        readBMP(bmp_file_path, reconstructedFilePath, binary_length / 8, encryptionKey);
    }

    Logger::Log(LOG_INFO, "Original file reconstructed successfully.");
    if (original_filename.size() > 5 && original_filename.compare(original_filename.size() - 5, 5, ".cfup") == 0) {
        Logger::Log(LOG_INFO, "Detected .cfup file, starting unpacking...");
        std::string unpackedFolder = (std::filesystem::path(save_path) / std::filesystem::path(original_filename).stem()).string();

        if (!unpack_packed_file(reconstructedFilePath, unpackedFolder)) {
            Logger::Log(LOG_ERROR, "Failed to unpack the .cfup file: " + reconstructedFilePath);
        } else {
            Logger::Log(LOG_INFO, "Unpacking completed successfully at: " + unpackedFolder);
        }
    }
}
