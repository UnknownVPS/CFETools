#ifndef ZIP_TO_CFUP_CONVERTER_H
#define ZIP_TO_CFUP_CONVERTER_H

#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>
#include <zip.h>  // libzip library
#include <algorithm>
#include "../../utils/logger/logger.h"

namespace fs = std::filesystem;

// Converts ZIP archive directly to CFUP format by streaming
bool convert_zip_to_cfup(const std::string& zipFilePath, const std::string& cfupFilePath) {
    int err = 0;
    zip_t* za = zip_open(zipFilePath.c_str(), ZIP_RDONLY, &err);
    if (!za) {
        zip_error_t error;
        zip_error_init_with_code(&error, err);
        Logger::Log(LOG_ERROR, "Failed to open ZIP file: " + std::string(zip_error_strerror(&error)));
        zip_error_fini(&error);
        return false;
    }

    zip_int64_t num_entries = zip_get_num_entries(za, 0);
    if (num_entries < 0) {
        Logger::Log(LOG_ERROR, "Failed to get number of entries in ZIP");
        zip_close(za);
        return false;
    }

    // Count actual files (skip directories) and collect with names for sorting
    struct ZipFileInfo {
        zip_int64_t index;
        std::string name;
    };
    
    std::vector<ZipFileInfo> file_list;
    for (zip_int64_t i = 0; i < num_entries; ++i) {
        struct zip_stat st;
        if (zip_stat_index(za, i, 0, &st) == 0) {
            // Check if it's a file (not directory)
            if (st.name[strlen(st.name) - 1] != '/') {
                std::string name = st.name;
                // Normalize path separators to forward slashes
                std::replace(name.begin(), name.end(), '\\', '/');
                file_list.push_back({i, name});
            }
        }
    }

    // CRITICAL: Sort files alphabetically for consistent ordering
    std::sort(file_list.begin(), file_list.end(), 
              [](const ZipFileInfo& a, const ZipFileInfo& b) {
                  return a.name < b.name;
              });

    uint32_t file_count = static_cast<uint32_t>(file_list.size());

    // Open output CFUP file
    std::ofstream out(cfupFilePath, std::ios::binary);
    if (!out) {
        Logger::Log(LOG_ERROR, "Failed to create CFUP file: " + cfupFilePath);
        zip_close(za);
        return false;
    }

    // Write file count header
    out.write(reinterpret_cast<const char*>(&file_count), sizeof(file_count));

    constexpr size_t buffer_size = 1024 * 1024 * 4; // 4MB buffer
    std::vector<char> buffer(buffer_size);

    // Process each file in ZIP (in sorted order)
    for (const auto& file_info : file_list) {
        zip_int64_t idx = file_info.index;
        struct zip_stat st;
        if (zip_stat_index(za, idx, 0, &st) != 0) {
            Logger::Log(LOG_ERROR, "Failed to get file stat for index " + std::to_string(idx));
            out.close();
            zip_close(za);
            return false;
        }

        std::string filename = st.name;
        // Normalize path separators to forward slashes (Unix style)
        std::replace(filename.begin(), filename.end(), '\\', '/');
        
        uint32_t path_len = static_cast<uint32_t>(filename.size());
        uint64_t data_size = st.size;

        // Write path length and path
        out.write(reinterpret_cast<const char*>(&path_len), sizeof(path_len));
        out.write(filename.data(), path_len);

        // Write data size
        out.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));

        // Open file in ZIP for reading
        zip_file_t* zf = zip_fopen_index(za, idx, 0);
        if (!zf) {
            Logger::Log(LOG_ERROR, "Failed to open file in ZIP: " + filename);
            out.close();
            zip_close(za);
            return false;
        }

        // Stream file data in chunks
        uint64_t bytes_left = data_size;
        while (bytes_left > 0) {
            size_t to_read = static_cast<size_t>(std::min<uint64_t>(bytes_left, buffer_size));
            zip_int64_t bytes_read = zip_fread(zf, buffer.data(), to_read);
            
            if (bytes_read < 0) {
                Logger::Log(LOG_ERROR, "Failed to read from ZIP file: " + filename);
                zip_fclose(zf);
                out.close();
                zip_close(za);
                return false;
            }

            if (bytes_read == 0 && bytes_left > 0) {
                Logger::Log(LOG_ERROR, "Unexpected EOF while reading: " + filename);
                zip_fclose(zf);
                out.close();
                zip_close(za);
                return false;
            }

            out.write(buffer.data(), bytes_read);
            if (!out) {
                Logger::Log(LOG_ERROR, "Failed to write data for: " + filename);
                zip_fclose(zf);
                out.close();
                zip_close(za);
                return false;
            }

            bytes_left -= bytes_read;
        }

        zip_fclose(zf);
    }

    out.close();
    zip_close(za);

    Logger::Log(LOG_INFO, "Converted ZIP to CFUP: " + std::to_string(file_count) + " files");
    return true;
}

// Converts CFUP back to ZIP format by streaming
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

    // Create ZIP archive
    int err = 0;
    zip_t* za = zip_open(zipFilePath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &err);
    if (!za) {
        zip_error_t error;
        zip_error_init_with_code(&error, err);
        Logger::Log(LOG_ERROR, "Failed to create ZIP file: " + std::string(zip_error_strerror(&error)));
        zip_error_fini(&error);
        return false;
    }

    constexpr size_t buffer_size = 1024 * 1024 * 4; // 4MB buffer
    std::vector<char> buffer(buffer_size);

    // Process each file
    for (uint32_t i = 0; i < file_count; ++i) {
        // Read path length and path
        uint32_t path_len;
        in.read(reinterpret_cast<char*>(&path_len), sizeof(path_len));
        if (in.fail()) {
            Logger::Log(LOG_ERROR, "Failed to read path length for file " + std::to_string(i));
            zip_close(za);
            return false;
        }

        std::string relative_path(path_len, '\0');
        in.read(&relative_path[0], path_len);
        if (in.fail()) {
            Logger::Log(LOG_ERROR, "Failed to read path for file " + std::to_string(i));
            zip_close(za);
            return false;
        }

        // Read data size
        uint64_t data_size;
        in.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));
        if (in.fail()) {
            Logger::Log(LOG_ERROR, "Failed to read data size for file " + std::to_string(i));
            zip_close(za);
            return false;
        }

        // Create zip source from CFUP data
        zip_source_t* source = nullptr;
        
        if (data_size == 0) {
            // Empty file
            source = zip_source_buffer(za, "", 0, 0);
        } else if (data_size <= buffer_size) {
            // Small file - read entirely
            in.read(buffer.data(), data_size);
            if (in.gcount() != static_cast<std::streamsize>(data_size)) {
                Logger::Log(LOG_ERROR, "Failed to read file data for: " + relative_path);
                zip_close(za);
                return false;
            }

            // Allocate and copy data for zip_source
            char* data_copy = new char[data_size];
            memcpy(data_copy, buffer.data(), data_size);
            source = zip_source_buffer(za, data_copy, data_size, 1); // 1 = free on close
        } else {
            // Large file - read entire content into memory
            std::vector<char> full_data(data_size);
            uint64_t bytes_left = data_size;
            size_t offset = 0;

            while (bytes_left > 0) {
                size_t to_read = static_cast<size_t>(std::min<uint64_t>(bytes_left, buffer_size));
                in.read(buffer.data(), to_read);
                if (in.gcount() != static_cast<std::streamsize>(to_read)) {
                    Logger::Log(LOG_ERROR, "Failed to read chunk for: " + relative_path);
                    zip_close(za);
                    return false;
                }
                memcpy(full_data.data() + offset, buffer.data(), to_read);
                offset += to_read;
                bytes_left -= to_read;
            }

            // Allocate and copy for zip_source
            char* data_copy = new char[data_size];
            memcpy(data_copy, full_data.data(), data_size);
            source = zip_source_buffer(za, data_copy, data_size, 1);
        }

        if (!source) {
            Logger::Log(LOG_ERROR, "Failed to create ZIP source for: " + relative_path);
            zip_close(za);
            return false;
        }

        // Add file to ZIP
        zip_int64_t idx = zip_file_add(za, relative_path.c_str(), source, ZIP_FL_OVERWRITE);
        if (idx < 0) {
            Logger::Log(LOG_ERROR, "Failed to add file to ZIP: " + relative_path);
            zip_source_free(source);
            zip_close(za);
            return false;
        }

        // Set compression method
        if (zip_set_file_compression(za, idx, ZIP_CM_DEFLATE, 6) < 0) {
            Logger::Log(LOG_ERROR, "Failed to set compression for: " + relative_path);
        }
    }

    if (zip_close(za) < 0) {
        Logger::Log(LOG_ERROR, "Failed to finalize ZIP archive");
        return false;
    }

    Logger::Log(LOG_INFO, "Converted CFUP to ZIP: " + std::to_string(file_count) + " files");
    return true;
}

#endif // ZIP_TO_CFUP_CONVERTER_H