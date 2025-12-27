#pragma once
#include <string>
#include <vector>
#include <map>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdint>

namespace fs = std::filesystem;

class FolderHasher {
public:
    struct FileInfo {
        std::string relative_path;
        std::string hash;
        uintmax_t size;
        fs::file_time_type last_write;
        
        bool operator<(const FileInfo& other) const {
            return relative_path < other.relative_path;
        }
    };
    
    struct ComparisonResult {
        std::vector<std::string> only_in_first;
        std::vector<std::string> only_in_second;
        std::vector<std::string> modified;
        std::vector<std::string> identical;
        std::string first_folder_hash;
        std::string second_folder_hash;
        bool are_identical;
    };

private:
    // Simple FNV-1a hash implementation
    static constexpr uint64_t FNV_OFFSET = 14695981039346656037ULL;
    static constexpr uint64_t FNV_PRIME = 1099511628211ULL;
    
    static uint64_t fnv1a_hash(const uint8_t* data, size_t length) {
        uint64_t hash = FNV_OFFSET;
        for (size_t i = 0; i < length; ++i) {
            hash ^= data[i];
            hash *= FNV_PRIME;
        }
        return hash;
    }
    
    static std::string to_hex(uint64_t value) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0') << std::setw(16) << value;
        return ss.str();
    }
    
    static std::string hash_file(const fs::path& filepath) {
        std::ifstream file(filepath, std::ios::binary);
        if (!file) {
            return "ERROR";
        }
        
        const size_t buffer_size = 8192;
        std::vector<uint8_t> buffer(buffer_size);
        uint64_t hash = FNV_OFFSET;
        
        while (file.read(reinterpret_cast<char*>(buffer.data()), buffer_size) || file.gcount() > 0) {
            size_t bytes_read = file.gcount();
            for (size_t i = 0; i < bytes_read; ++i) {
                hash ^= buffer[i];
                hash *= FNV_PRIME;
            }
        }
        
        return to_hex(hash);
    }
    
    static std::vector<FileInfo> scan_folder(const fs::path& folder_path, bool recursive = true) {
        std::vector<FileInfo> files;
        
        if (!fs::exists(folder_path) || !fs::is_directory(folder_path)) {
            return files;
        }
        
        if (recursive) {
            for (const auto& entry : fs::recursive_directory_iterator(folder_path, fs::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file()) {
                    FileInfo info;
                    info.relative_path = fs::relative(entry.path(), folder_path).string();
                    info.size = entry.file_size();
                    info.last_write = entry.last_write_time();
                    info.hash = hash_file(entry.path());
                    files.push_back(info);
                }
            }
        } else {
            for (const auto& entry : fs::directory_iterator(folder_path, fs::directory_options::skip_permission_denied)) {
                if (entry.is_regular_file()) {
                    FileInfo info;
                    info.relative_path = fs::relative(entry.path(), folder_path).string();
                    info.size = entry.file_size();
                    info.last_write = entry.last_write_time();
                    info.hash = hash_file(entry.path());
                    files.push_back(info);
                }
            }
        }
        
        std::sort(files.begin(), files.end());
        return files;
    }
    
    static std::string compute_folder_hash(const std::vector<FileInfo>& files) {
        std::string combined;
        for (const auto& file : files) {
            combined += file.relative_path + ":" + file.hash + ":" + std::to_string(file.size) + ";";
        }
        
        uint64_t hash = fnv1a_hash(reinterpret_cast<const uint8_t*>(combined.data()), combined.size());
        return to_hex(hash);
    }

public:
    // Hash a single folder
    static std::string hash_folder(const std::string& folder_path, bool recursive = true) {
        auto files = scan_folder(folder_path, recursive);
        return compute_folder_hash(files);
    }
    
    // Get detailed file information
    static std::vector<FileInfo> get_folder_contents(const std::string& folder_path, bool recursive = true) {
        return scan_folder(folder_path, recursive);
    }
    
    // Compare two folders
    static ComparisonResult compare_folders(const std::string& folder1, const std::string& folder2, bool recursive = true) {
        ComparisonResult result;
        
        auto files1 = scan_folder(folder1, recursive);
        auto files2 = scan_folder(folder2, recursive);
        
        result.first_folder_hash = compute_folder_hash(files1);
        result.second_folder_hash = compute_folder_hash(files2);
        
        // Create maps for quick lookup
        std::map<std::string, FileInfo> map1, map2;
        for (const auto& f : files1) map1[f.relative_path] = f;
        for (const auto& f : files2) map2[f.relative_path] = f;
        
        // Find files only in first folder
        for (const auto& [path, info] : map1) {
            if (map2.find(path) == map2.end()) {
                result.only_in_first.push_back(path);
            }
        }
        
        // Find files only in second folder
        for (const auto& [path, info] : map2) {
            if (map1.find(path) == map1.end()) {
                result.only_in_second.push_back(path);
            }
        }
        
        // Find modified and identical files
        for (const auto& [path, info1] : map1) {
            auto it = map2.find(path);
            if (it != map2.end()) {
                const auto& info2 = it->second;
                if (info1.hash == info2.hash && info1.size == info2.size) {
                    result.identical.push_back(path);
                } else {
                    result.modified.push_back(path);
                }
            }
        }
        
        result.are_identical = result.only_in_first.empty() && 
                              result.only_in_second.empty() && 
                              result.modified.empty();
        
        return result;
    }
    
    // Quick check if two folders are identical
    static bool are_folders_identical(const std::string& folder1, const std::string& folder2, bool recursive = true) {
        return hash_folder(folder1, recursive) == hash_folder(folder2, recursive);
    }
    
    // Export folder structure to file
    static bool export_folder_info(const std::string& folder_path, const std::string& output_file, bool recursive = true) {
        auto files = scan_folder(folder_path, recursive);
        
        std::ofstream out(output_file);
        if (!out) return false;
        
        out << "Folder: " << folder_path << "\n";
        out << "Hash: " << compute_folder_hash(files) << "\n";
        out << "Total Files: " << files.size() << "\n\n";
        
        for (const auto& file : files) {
            out << file.relative_path << "\n";
            out << "  Hash: " << file.hash << "\n";
            out << "  Size: " << file.size << " bytes\n\n";
        }
        
        return true;
    }
};  // FOLDER_HASHER_HPP