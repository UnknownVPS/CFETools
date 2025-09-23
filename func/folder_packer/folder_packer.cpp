#include "folder_packer.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <cstdint>
#include <unordered_set>
#include <algorithm>

namespace fs = std::filesystem;

// Simple logger placeholders (replace with your Logger::Log calls)
// #include "logger/logger.h"
// use Logger::Log(LOG_INFO, ...);

struct TOCEntry {
    std::string relative_path;
    uint64_t size;
    uint64_t offset; // offset from beginning of file where the file data resides
};

// Packs files using a TOC (table-of-contents) approach:
// [file_count(uint32)]
// repeated for file_count:
//   [path_len(uint32)][path bytes][data_size(uint64)][offset(uint64)]
// followed by file data concatenated in the same order as TOC entries.

bool pack_folder_toc(const std::string& folderPath, const std::string& packedFilePath) {
    fs::path folder(folderPath);
    if (!fs::exists(folder) || !fs::is_directory(folder)) {
        Logger::Log(LOG_ERROR, "Input is not a valid folder: " + folderPath);
        return false;
    }

    // Normalize and ensure folder path ends with separator for easy substr
    fs::path abs_folder = fs::absolute(folder);
    std::string folder_str = abs_folder.string();
#ifdef _WIN32
    const char sep = '\\';
#else
    const char sep = '/';
#endif
    if (!folder_str.empty() && folder_str.back() != sep) folder_str.push_back(sep);

    // Collect files
    std::vector<fs::path> files;
    files.reserve(1024);
    for (const auto& it : fs::recursive_directory_iterator(abs_folder)) {
        if (fs::is_regular_file(it.path())) files.push_back(it.path());
    }

    // Build TOC entries
    std::vector<TOCEntry> toc;
    toc.reserve(files.size());
    for (const auto& p : files) {
        std::string pstr = p.string();
        // safety: ensure path begins with folder_str
        if (pstr.size() <= folder_str.size()) continue; // skip weird cases
        std::string rel = pstr.substr(folder_str.size());
        TOCEntry e;
        e.relative_path = std::move(rel);
        e.size = static_cast<uint64_t>(fs::file_size(p));
        e.offset = 0; // fill in below
        toc.push_back(std::move(e));
    }

    // Calculate offsets: header size = 4 bytes for file_count
    // each TOC entry has: 4 bytes path_len + path bytes + 8 bytes size + 8 bytes offset
    uint64_t header_size = sizeof(uint32_t);
    for (const auto& e : toc) {
        header_size += sizeof(uint32_t); // path_len
        header_size += static_cast<uint64_t>(e.relative_path.size());
        header_size += sizeof(uint64_t); // data_size
        header_size += sizeof(uint64_t); // offset
    }

    // Offsets start right after header (header_size)
    uint64_t current_data_offset = header_size;
    for (auto& e : toc) {
        e.offset = current_data_offset;
        current_data_offset += e.size;
    }

    std::ofstream out(packedFilePath, std::ios::binary);
    if (!out) {
        Logger::Log(LOG_ERROR, "Failed to create packed file: " + packedFilePath);
        return false;
    }

    // Write file_count
    uint32_t file_count = static_cast<uint32_t>(toc.size());
    out.write(reinterpret_cast<const char*>(&file_count), sizeof(file_count));
    if (!out) { Logger::Log(LOG_ERROR, "Write failed (file_count)"); return false; }

    // Write TOC
    for (const auto& e : toc) {
        uint32_t path_len = static_cast<uint32_t>(e.relative_path.size());
        out.write(reinterpret_cast<const char*>(&path_len), sizeof(path_len));
        out.write(e.relative_path.data(), path_len);
        out.write(reinterpret_cast<const char*>(&e.size), sizeof(e.size));
        out.write(reinterpret_cast<const char*>(&e.offset), sizeof(e.offset));
        if (!out) { Logger::Log(LOG_ERROR, "Write failed (TOC entry)"); return false; }
    }

    // Now write file data sequentially in the same order as TOC
    constexpr size_t buffer_size = 1024 * 1024 * 4; // 4MB buffer
    std::vector<char> buffer(buffer_size);

    for (size_t i = 0; i < toc.size(); ++i) {
        const auto& e = toc[i];
        fs::path src = fs::path(folder_str) / e.relative_path;
        std::ifstream in(src, std::ios::binary);
        if (!in) { Logger::Log(LOG_ERROR, "Failed to open source file: " + src.string()); return false; }

        uint64_t bytes_left = e.size;
        while (bytes_left > 0) {
            size_t to_read = static_cast<size_t>(std::min<uint64_t>(bytes_left, buffer_size));
            in.read(buffer.data(), static_cast<std::streamsize>(to_read));
            std::streamsize got = in.gcount();
            if (got <= 0) { Logger::Log(LOG_ERROR, "Failed to read from source: " + src.string()); return false; }
            out.write(buffer.data(), got);
            if (!out) { Logger::Log(LOG_ERROR, "Failed writing data to packed file"); return false; }
            bytes_left -= static_cast<uint64_t>(got);
        }
    }

    Logger::Log(LOG_INFO, "Packed " + std::to_string(file_count) + " files into " + packedFilePath);
    return true;
}


bool unpack_packed_file_toc(const std::string& packedFilePath, const std::string& outputFolderPath) {
    std::ifstream in(packedFilePath, std::ios::binary);
    if (!in) {
        Logger::Log(LOG_ERROR, "Failed to open packed file for reading: " + packedFilePath);
        return false;
    }

    uint32_t file_count = 0;
    in.read(reinterpret_cast<char*>(&file_count), sizeof(file_count));
    if (in.fail()) { Logger::Log(LOG_ERROR, "Failed to read file count from packed file."); return false; }

    std::vector<TOCEntry> toc;
    toc.reserve(file_count);
    for (uint32_t i = 0; i < file_count; ++i) {
        uint32_t path_len = 0;
        in.read(reinterpret_cast<char*>(&path_len), sizeof(path_len));
        if (in.fail()) { Logger::Log(LOG_ERROR, "Failed to read path_len at index " + std::to_string(i)); return false; }

        std::string path(path_len, '\0');
        in.read(&path[0], path_len);
        if (in.gcount() != static_cast<std::streamsize>(path_len)) { Logger::Log(LOG_ERROR, "Failed to read path string at index " + std::to_string(i)); return false; }

        uint64_t data_size = 0, offset = 0;
        in.read(reinterpret_cast<char*>(&data_size), sizeof(data_size));
        in.read(reinterpret_cast<char*>(&offset), sizeof(offset));
        if (in.fail()) { Logger::Log(LOG_ERROR, "Failed to read size/offset at index " + std::to_string(i)); return false; }

        TOCEntry e;
        e.relative_path = std::move(path);
        e.size = data_size;
        e.offset = offset;
        toc.push_back(std::move(e));
    }

    fs::path out_root = fs::absolute(outputFolderPath);
    // Create directories in batch: cache created dirs to avoid repeated syscalls
    std::unordered_set<std::string> created_dirs;

    constexpr size_t buffer_size = 1024 * 1024 * 4;
    std::vector<char> buffer(buffer_size);

    for (size_t i = 0; i < toc.size(); ++i) {
        const auto& e = toc[i];
        fs::path out_path = out_root / e.relative_path;
        fs::path parent = out_path.parent_path();
        std::string parent_str = parent.string();

        if (!parent_str.empty()) {
            if (created_dirs.insert(parent_str).second) {
                std::error_code ec;
                fs::create_directories(parent, ec);
                if (ec) { Logger::Log(LOG_ERROR, "Failed to create directory: " + parent_str + " -> " + ec.message()); return false; }
            }
        }

        std::ofstream out_file(out_path, std::ios::binary);
        if (!out_file) { Logger::Log(LOG_ERROR, "Failed to create output file: " + out_path.string()); return false; }

        // Seek to the data offset and copy
        in.seekg(static_cast<std::streamoff>(e.offset), std::ios::beg);
        if (!in) { Logger::Log(LOG_ERROR, "seekg failed for offset " + std::to_string(e.offset)); return false; }

        uint64_t bytes_left = e.size;
        while (bytes_left > 0) {
            size_t to_read = static_cast<size_t>(std::min<uint64_t>(bytes_left, buffer_size));
            in.read(buffer.data(), static_cast<std::streamsize>(to_read));
            std::streamsize got = in.gcount();
            if (got <= 0) { Logger::Log(LOG_ERROR, "Failed to read packed data for file index " + std::to_string(i)); return false; }
            out_file.write(buffer.data(), got);
            if (!out_file) { Logger::Log(LOG_ERROR, "Failed to write to output file: " + out_path.string()); return false; }
            bytes_left -= static_cast<uint64_t>(got);
        }
    }

    Logger::Log(LOG_INFO, "Unpacked " + std::to_string(toc.size()) + " files into " + outputFolderPath);
    return true;
}
