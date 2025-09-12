#include "folder_packer.h"

namespace fs = std::filesystem;

// Packs files by streaming their content directly from disk into packed archive
bool pack_folder(const std::string& folderPath, const std::string& packedFilePath) {
    fs::path folder(folderPath);

    if (!fs::exists(folder) || !fs::is_directory(folder)) {
        Logger::Log(LOG_ERROR, "Input is not a valid folder: " + folderPath);
        return false;
    }

    // Collect all regular files recursively
    std::vector<fs::path> file_paths;
    for (const auto& p : fs::recursive_directory_iterator(folder)) {
        if (fs::is_regular_file(p)) {
            file_paths.push_back(p.path());
        }
    }

    std::ofstream out(packedFilePath, std::ios::binary);
    if (!out) {
        Logger::Log(LOG_ERROR, "Failed to create packed file: " + packedFilePath);
        return false;
    }

    const uint32_t file_count = static_cast<uint32_t>(file_paths.size());
    out.write(reinterpret_cast<const char*>(&file_count), sizeof(file_count));

    constexpr size_t buffer_size = 1024 * 1024 * 4; // 1MB buffer
    std::vector<char> buffer(buffer_size);

    for (const fs::path& file_path : file_paths) {
        std::string relative_path = fs::relative(file_path, folder).string();

        uint32_t path_len = static_cast<uint32_t>(relative_path.size());
        out.write(reinterpret_cast<const char*>(&path_len), sizeof(path_len));
        out.write(relative_path.data(), path_len);

        std::ifstream in_file(file_path, std::ios::binary | std::ios::ate);
        if (!in_file) {
            Logger::Log(LOG_ERROR, "Failed to open file for reading: " + file_path.string());
            return false;
        }

        std::streamsize file_size = in_file.tellg();
        in_file.seekg(0, std::ios::beg);

        uint64_t data_size = static_cast<uint64_t>(file_size);
        out.write(reinterpret_cast<const char*>(&data_size), sizeof(data_size));

        while (file_size > 0) {
            std::streamsize to_read = std::min(file_size, static_cast<std::streamsize>(buffer_size));
            if (!in_file.read(buffer.data(), to_read)) {
                Logger::Log(LOG_ERROR, "Failed to read data from file: " + file_path.string());
                return false;
            }
            out.write(buffer.data(), to_read);
            if (!out) {
                Logger::Log(LOG_ERROR, "Failed to write data to packed file.");
                return false;
            }
            file_size -= to_read;
        }
    }

    Logger::Log(LOG_INFO, "Packed " + std::to_string(file_count) + " files into " + packedFilePath);
    return true;
}

// Unpacks files by streaming data from packed archive to disk individually
bool unpack_packed_file(const std::string& packedFilePath, const std::string& outputFolderPath) {
    std::ifstream in(packedFilePath, std::ios::binary);
    if (!in) {
        Logger::Log(LOG_ERROR, "Failed to open packed file for reading: " + packedFilePath);
        return false;
    }

    uint32_t file_count;
    in.read(reinterpret_cast<char*>(&file_count), sizeof(file_count));
    if (in.fail()) {
        Logger::Log(LOG_ERROR, "Failed to read file count from packed file.");
        return false;
    }

    fs::path unpackedFolderPath(outputFolderPath);
    constexpr size_t buffer_size = 1024 * 1024 * 4; // 1MB buffer
    std::vector<char> buffer(buffer_size);

    for (uint32_t i = 0; i < file_count; ++i) {
        uint32_t path_len;
        in.read(reinterpret_cast<char*>(&path_len), sizeof(path_len));
        if (in.fail()) {
            Logger::Log(LOG_ERROR, "Failed to read path length for file index " + std::to_string(i));
            return false;
        }

        std::string relative_path(path_len, '\0');
        in.read(&relative_path[0], path_len);
        if (in.fail()) {
            Logger::Log(LOG_ERROR, "Failed to read path string for file index " + std::to_string(i));
            return false;
        }

        uint64_t data_size;
        in.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));
        if (in.fail()) {
            Logger::Log(LOG_ERROR, "Failed to read data size for file index " + std::to_string(i));
            return false;
        }

        fs::path out_path = unpackedFolderPath / relative_path;
        fs::create_directories(out_path.parent_path());

        std::ofstream out_file(out_path, std::ios::binary);
        if (!out_file) {
            Logger::Log(LOG_ERROR, "Failed to create output file: " + out_path.string());
            return false;
        }

        uint64_t bytes_left = data_size;
        while (bytes_left > 0) {
            size_t to_read = static_cast<size_t>(std::min<uint64_t>(bytes_left, buffer_size));
            in.read(buffer.data(), to_read);
            if (in.gcount() != static_cast<std::streamsize>(to_read)) {
                Logger::Log(LOG_ERROR, "Failed to read file data for file index " + std::to_string(i));
                return false;
            }
            out_file.write(buffer.data(), to_read);
            if (!out_file) {
                Logger::Log(LOG_ERROR, "Failed to write output file data: " + out_path.string());
                return false;
            }
            bytes_left -= to_read;
        }
    }

    Logger::Log(LOG_INFO, "Unpacked " + std::to_string(file_count) + " files into " + outputFolderPath);
    return true;
}