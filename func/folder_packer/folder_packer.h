#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include "../../utils/logger/logger.h"
namespace fs = std::filesystem;

using WriteFn = std::function<bool(const void*, size_t)>;
using ReadFn  = std::function<size_t(void*, size_t)>;

struct PackedFileEntry {
    std::string path;
    uint64_t    offset;   // byte offset to data, from start of file
    uint64_t    size;
    uint64_t    xxh64;    
};

// ── Pack ───────────────────────────────────────────────────────

bool pack_folder_stream(const std::string& folderPath, WriteFn dst,
                        bool follow_symlinks = false);

bool pack_folder(const std::string& folderPath,
                 const std::string& packedFilePath,
                 bool follow_symlinks = false);

// ── Unpack ─────────────────────────────────────────────────────

bool unpack_stream(ReadFn src, const std::string& outputFolderPath,
                   bool verify = true);

bool unpack_packed_file(const std::string& packedFilePath,
                        const std::string& outputFolderPath,
                        bool verify = true);

// ── TOC / random access ────────────────────────────────────────

bool list_packed_files(const std::string& packedFilePath,
                       std::vector<PackedFileEntry>& out_entries);

bool extract_file(const std::string& packedFilePath,
                  uint32_t file_index,
                  const std::string& outputPath,
                  bool verify = true);

// ── Probe ──────────────────────────────────────────────────────

bool looks_like_cfup_header(const uint8_t* bytes, size_t len);
bool looks_like_cfup(const std::string& filePath);

// Migrator
bool migrate_cfup_v1_to_v2(const std::string& v1_path, const std::string& v2_path);