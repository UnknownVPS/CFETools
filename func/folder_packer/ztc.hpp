#ifndef ZIP_TO_CFUP_CONVERTER_H
#define ZIP_TO_CFUP_CONVERTER_H

#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <memory>
#include <zip.h>
#include "../../utils/logger/logger.h"

namespace fs = std::filesystem;

constexpr size_t CHUNK_SIZE = 1024 * 1024 * 8; // 8MB chunks for streaming

// RAII wrapper for libzip archive
class ZipArchive {
private:
    zip_t* archive_ = nullptr;

public:
    explicit ZipArchive(const std::string& path, int flags = 0) {
        int error = 0;
        archive_ = zip_open(path.c_str(), flags, &error);
        if (!archive_) {
            zip_error_t zip_error;
            zip_error_init_with_code(&zip_error, error);
            throw std::runtime_error("Failed to open ZIP: " + std::string(zip_error_strerror(&zip_error)));
        }
    }

    ~ZipArchive() {
        if (archive_) {
            zip_close(archive_);
        }
    }

    ZipArchive(const ZipArchive&) = delete;
    ZipArchive& operator=(const ZipArchive&) = delete;

    zip_t* get() { return archive_; }
};

// RAII wrapper for libzip file handle
class ZipFile {
private:
    zip_file_t* file_ = nullptr;

public:
    explicit ZipFile(zip_t* archive, zip_uint64_t index) {
        file_ = zip_fopen_index(archive, index, 0);
        if (!file_) {
            throw std::runtime_error("Failed to open file in ZIP archive");
        }
    }

    ~ZipFile() {
        if (file_) {
            zip_fclose(file_);
        }
    }

    ZipFile(const ZipFile&) = delete;
    ZipFile& operator=(const ZipFile&) = delete;

    zip_int64_t read(void* buffer, zip_uint64_t size) {
        return zip_fread(file_, buffer, size);
    }
};

// Converts ZIP archive to CFUP format with streaming
bool convert_zip_to_cfup(const std::string& zipFilePath, const std::string& cfupFilePath) {
    try {
        // Open ZIP archive using libzip
        ZipArchive zip_archive(zipFilePath);
        zip_t* zip = zip_archive.get();

        // Get number of entries
        zip_int64_t num_entries = zip_get_num_entries(zip, 0);
        if (num_entries < 0) {
            Logger::Log(LOG_ERROR, "Failed to get ZIP entry count");
            return false;
        }

        // Collect file entries (skip directories)
        struct FileEntry {
            std::string name;
            zip_uint64_t index;
            zip_uint64_t size;
        };

        std::vector<FileEntry> files;
        files.reserve(num_entries);

        for (zip_int64_t i = 0; i < num_entries; ++i) {
            zip_stat_t stat;
            if (zip_stat_index(zip, i, 0, &stat) != 0) {
                Logger::Log(LOG_ERROR, "Failed to stat entry at index " + std::to_string(i));
                continue;
            }

            std::string name = stat.name;
            
            // Skip directories (names ending with '/')
            if (name.empty() || name.back() == '/') {
                continue;
            }

            // Normalize path separators
            std::replace(name.begin(), name.end(), '\\', '/');

            files.push_back({name, static_cast<zip_uint64_t>(i), stat.size});
        }

        // Sort files alphabetically for consistent output
        std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
            return a.name < b.name;
        });

        uint32_t file_count = static_cast<uint32_t>(files.size());

        // Open output CFUP file
        std::ofstream out(cfupFilePath, std::ios::binary);
        if (!out) {
            Logger::Log(LOG_ERROR, "Failed to create CFUP file: " + cfupFilePath);
            return false;
        }

        // Write file count header
        out.write(reinterpret_cast<const char*>(&file_count), sizeof(file_count));

        // Streaming buffer
        std::vector<uint8_t> buffer(CHUNK_SIZE);

        // Process each file with streaming
        for (const auto& file : files) {
            uint32_t path_len = static_cast<uint32_t>(file.name.size());
            uint64_t data_size = file.size;

            // Write path length and path
            out.write(reinterpret_cast<const char*>(&path_len), sizeof(path_len));
            out.write(file.name.data(), path_len);

            // Write data size
            out.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));

            // Stream file data using libzip
            // libzip automatically handles decompression
            ZipFile zip_file(zip, file.index);
            
            uint64_t remaining = data_size;
            while (remaining > 0) {
                size_t to_read = std::min<uint64_t>(remaining, CHUNK_SIZE);
                zip_int64_t bytes_read = zip_file.read(buffer.data(), to_read);
                
                if (bytes_read < 0) {
                    Logger::Log(LOG_ERROR, "Failed to read from ZIP file: " + file.name);
                    return false;
                }
                
                if (bytes_read == 0) {
                    break; // EOF
                }

                out.write(reinterpret_cast<const char*>(buffer.data()), bytes_read);
                remaining -= bytes_read;
            }

            if (remaining > 0) {
                Logger::Log(LOG_ERROR, "Incomplete read for file: " + file.name);
                return false;
            }
        }

        out.close();
        Logger::Log(LOG_INFO, "Converted ZIP to CFUP: " + std::to_string(file_count) + " files");
        return true;

    } catch (const std::exception& e) {
        Logger::Log(LOG_ERROR, "Exception during ZIP to CFUP conversion: " + std::string(e.what()));
        return false;
    }
}

// Custom source callback for streaming from CFUP to ZIP
struct CFUPSourceData {
    std::ifstream* input;
    uint64_t file_size;
    uint64_t bytes_read;
    std::vector<uint8_t> buffer;

    CFUPSourceData(std::ifstream* in, uint64_t size) 
        : input(in), file_size(size), bytes_read(0), buffer(CHUNK_SIZE) {}
};

// Callback function for zip_source_function
static zip_int64_t cfup_source_callback(void* userdata, void* data, zip_uint64_t len, zip_source_cmd_t cmd) {
    CFUPSourceData* src = static_cast<CFUPSourceData*>(userdata);

    switch (cmd) {
        case ZIP_SOURCE_OPEN:
            src->bytes_read = 0;
            return 0;

        case ZIP_SOURCE_READ: {
            if (src->bytes_read >= src->file_size) {
                return 0; // EOF
            }

            uint64_t remaining = src->file_size - src->bytes_read;
            size_t to_read = std::min<uint64_t>(len, remaining);
            
            src->input->read(reinterpret_cast<char*>(data), to_read);
            size_t actually_read = src->input->gcount();
            
            src->bytes_read += actually_read;
            return actually_read;
        }

        case ZIP_SOURCE_CLOSE:
            return 0;

        case ZIP_SOURCE_STAT: {
            zip_stat_t* stat = static_cast<zip_stat_t*>(data);
            zip_stat_init(stat);
            stat->size = src->file_size;
            stat->valid = ZIP_STAT_SIZE;
            return sizeof(zip_stat_t);
        }

        case ZIP_SOURCE_ERROR: {
            zip_error_t zip_error;
            zip_error_init_with_code(&zip_error, ZIP_ER_INTERNAL);
            zip_int64_t ret = zip_error_to_data(&zip_error, data, len);
            return ret;
        }

        case ZIP_SOURCE_FREE:
            delete src;
            return 0;

        default:
            return -1;
    }
}

// Converts CFUP back to ZIP format with streaming compression
bool convert_cfup_to_zip(const std::string& cfupFilePath, const std::string& zipFilePath) {
    std::ifstream in(cfupFilePath, std::ios::binary);
    if (!in) {
        Logger::Log(LOG_ERROR, "Failed to open CFUP file: " + cfupFilePath);
        return false;
    }

    // Read file count
    uint32_t file_count;
    in.read(reinterpret_cast<char*>(&file_count), sizeof(file_count));
    if (in.fail()) {
        Logger::Log(LOG_ERROR, "Failed to read file count from CFUP");
        return false;
    }

    try {
        // Create new ZIP archive
        int error = 0;
        zip_t* zip = zip_open(zipFilePath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
        if (!zip) {
            zip_error_t zip_error;
            zip_error_init_with_code(&zip_error, error);
            Logger::Log(LOG_ERROR, "Failed to create ZIP: " + std::string(zip_error_strerror(&zip_error)));
            return false;
        }

    // Per-file compression is set below with zip_set_file_compression.
    // (Removed invalid call to zip_set_default_compression which is not part of libzip's public API.)

        // Process each file
        for (uint32_t i = 0; i < file_count; ++i) {
            // Read path length and path
            uint32_t path_len;
            in.read(reinterpret_cast<char*>(&path_len), sizeof(path_len));
            
            std::string relative_path(path_len, '\0');
            in.read(&relative_path[0], path_len);

            // Read data size
            uint64_t data_size;
            in.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));

            // Create streaming source for this file
            // Note: CFUPSourceData will be deleted by ZIP_SOURCE_FREE callback
            CFUPSourceData* src_data = new CFUPSourceData(&in, data_size);
            
            zip_source_t* source = zip_source_function(zip, cfup_source_callback, src_data);
            if (!source) {
                Logger::Log(LOG_ERROR, "Failed to create ZIP source for: " + relative_path);
                delete src_data;
                zip_close(zip);
                return false;
            }

            // Add file to ZIP with streaming source
            zip_int64_t idx = zip_file_add(zip, relative_path.c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
            if (idx < 0) {
                Logger::Log(LOG_ERROR, "Failed to add file to ZIP: " + relative_path);
                zip_source_free(source);
                zip_close(zip);
                return false;
            }

            // Set compression method (DEFLATE with level 6)
            zip_set_file_compression(zip, idx, ZIP_CM_DEFLATE, 6);
        }

        // Finalize and close ZIP
        if (zip_close(zip) != 0) {
            Logger::Log(LOG_ERROR, "Failed to finalize ZIP file");
            return false;
        }

        in.close();
        Logger::Log(LOG_INFO, "Converted CFUP to ZIP: " + std::to_string(file_count) + " files");
        return true;

    } catch (const std::exception& e) {
        Logger::Log(LOG_ERROR, "Exception during CFUP to ZIP conversion: " + std::string(e.what()));
        in.close();
        return false;
    }
}

#endif // ZIP_TO_CFUP_CONVERTER_H