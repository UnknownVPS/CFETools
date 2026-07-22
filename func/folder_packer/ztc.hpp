#ifndef ZIP_TO_CFUP_CONVERTER_H
#define ZIP_TO_CFUP_CONVERTER_H

#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include <cstdint>
#include <algorithm>
#include <memory>
#include <cstring>
#include <zip.h>
#include "../../utils/logger/logger.h"

// XXH64 required for v2 hashing
#define XXH_INLINE_ALL
#include "../../utils/hashers/xxhash.h"

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

    zip_int64_t read(void* buffer, zip_uint64_t size) {
        return zip_fread(file_, buffer, size);
    }
};

// ─────────────────────────────────────────────────────────────────
//  Little-Endian helpers (Local to avoid coupling dependencies)
// ─────────────────────────────────────────────────────────────────

static bool w_u16(std::ofstream& f, uint16_t v) {
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    f.write(reinterpret_cast<const char*>(b), 2); return f.good();
}
static bool w_u32(std::ofstream& f, uint32_t v) {
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    f.write(reinterpret_cast<const char*>(b), 4); return f.good();
}
static bool w_u64(std::ofstream& f, uint64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = (uint8_t)(v >> (i * 8));
    f.write(reinterpret_cast<const char*>(b), 8); return f.good();
}

static uint16_t r_u16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t r_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t r_u64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (i * 8);
    return v;
}

// ─────────────────────────────────────────────────────────────────
//  ZIP -> CFUP v2
// ─────────────────────────────────────────────────────────────────

bool convert_zip_to_cfup(const std::string& zipFilePath, const std::string& cfupFilePath) {
    try {
        ZipArchive zip_archive(zipFilePath);
        zip_t* zip = zip_archive.get();

        zip_int64_t num_entries = zip_get_num_entries(zip, 0);
        if (num_entries < 0) {
            Logger::Log(LOG_ERROR, "Failed to get ZIP entry count");
            return false;
        }

        struct FileEntry {
            std::string name;
            zip_uint64_t index;
            zip_uint64_t size;
        };

        std::vector<FileEntry> files;
        files.reserve(num_entries);

        for (zip_int64_t i = 0; i < num_entries; ++i) {
            zip_stat_t stat;
            if (zip_stat_index(zip, i, 0, &stat) != 0) continue;

            std::string name = stat.name;
            if (name.empty() || name.back() == '/') continue;

            std::replace(name.begin(), name.end(), '\\', '/');

            // V2 Security: Block directory traversal from ZIPs
            if (name[0] == '/' || name.find("..") != std::string::npos) {
                Logger::Log(LOG_ERROR, "Unsafe path blocked in ZIP: " + name);
                return false;
            }

            files.push_back({name, static_cast<zip_uint64_t>(i), stat.size});
        }

        std::sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
            return a.name < b.name;
        });

        uint32_t file_count = static_cast<uint32_t>(files.size());

        std::ofstream out(cfupFilePath, std::ios::binary);
        if (!out) {
            Logger::Log(LOG_ERROR, "Failed to create CFUP file: " + cfupFilePath);
            return false;
        }

        // ── Write CFUP v2 Header (16 bytes) ──
        out.write("CFUP", 4);
        w_u16(out, 2); // version
        w_u16(out, 0); // flags
        w_u32(out, file_count);
        w_u32(out, 0); // reserved

        uint64_t cur = 16; // CFUP_HEADER_SIZE

        struct TOCEntry {
            std::string path;
            uint64_t data_offset;
            uint64_t data_size;
            uint64_t xxh64;
        };
        std::vector<TOCEntry> toc;
        toc.reserve(file_count);

        std::vector<uint8_t> buffer(CHUNK_SIZE);

        // ── Stream Files ──
        for (const auto& file : files) {
            uint32_t path_len = static_cast<uint32_t>(file.name.size());
            uint64_t data_size = file.size;
            
            // Calculate exact byte offset where data starts
            uint64_t data_offset = cur + 4 + path_len + 8;

            // Write Entry Header
            w_u32(out, path_len);
            out.write(file.name.data(), path_len);
            w_u64(out, data_size);

            // Stream data and calculate hash simultaneously
            ZipFile zip_file(zip, file.index);
            XXH64_state_t hash_state;
            XXH64_reset(&hash_state, 0);
            
            uint64_t remaining = data_size;
            while (remaining > 0) {
                size_t to_read = std::min<uint64_t>(remaining, CHUNK_SIZE);
                zip_int64_t bytes_read = zip_file.read(buffer.data(), to_read);
                
                if (bytes_read < 0) {
                    Logger::Log(LOG_ERROR, "Failed to read from ZIP: " + file.name);
                    return false;
                }
                if (bytes_read == 0) break;

                XXH64_update(&hash_state, buffer.data(), bytes_read);
                out.write(reinterpret_cast<const char*>(buffer.data()), bytes_read);
                remaining -= bytes_read;
            }

            if (remaining > 0) {
                Logger::Log(LOG_ERROR, "Incomplete read for file: " + file.name);
                return false;
            }

            // Write V2 Hash
            uint64_t hash = XXH64_digest(&hash_state);
            w_u64(out, hash);

            cur = data_offset + data_size + 8;
            toc.push_back({file.name, data_offset, data_size, hash});
        }

        // ── Write TOC ──
        uint64_t toc_offset = cur;
        for (const auto& e : toc) {
            w_u32(out, static_cast<uint32_t>(e.path.size()));
            out.write(e.path.data(), e.path.size());
            w_u64(out, e.data_offset);
            w_u64(out, e.data_size);
            w_u64(out, e.xxh64);
        }

        // ── Write Footer (20 bytes) ──
        out.write("CFUP", 4);
        w_u16(out, 2); // version
        w_u16(out, 0); // flags
        w_u32(out, file_count);
        w_u64(out, toc_offset);

        out.close();
        Logger::Log(LOG_INFO, "Converted ZIP to CFUP v2: " + std::to_string(file_count) + " files");
        return true;

    } catch (const std::exception& e) {
        Logger::Log(LOG_ERROR, "Exception during ZIP to CFUP conversion: " + std::string(e.what()));
        return false;
    }
}

// ─────────────────────────────────────────────────────────────────
//  CFUP v2 -> ZIP
// ─────────────────────────────────────────────────────────────────

// Custom source callback for streaming from CFUP to ZIP
struct CFUPSourceData {
    std::ifstream* input;
    uint64_t file_size;
    uint64_t bytes_read;

    CFUPSourceData(std::ifstream* in, uint64_t size) 
        : input(in), file_size(size), bytes_read(0) {}
};

static zip_int64_t cfup_source_callback(void* userdata, void* data, zip_uint64_t len, zip_source_cmd_t cmd) {
    CFUPSourceData* src = static_cast<CFUPSourceData*>(userdata);

    switch (cmd) {
        case ZIP_SOURCE_OPEN:
            src->bytes_read = 0;
            return 0;

        case ZIP_SOURCE_READ: {
            if (src->bytes_read >= src->file_size) return 0;
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
            return zip_error_to_data(&zip_error, data, len);
        }

        case ZIP_SOURCE_FREE:
            delete src;
            return 0;

        default:
            return -1;
    }
}

bool convert_cfup_to_zip(const std::string& cfupFilePath, const std::string& zipFilePath) {
    std::ifstream in(cfupFilePath, std::ios::binary);
    if (!in) {
        Logger::Log(LOG_ERROR, "Failed to open CFUP file: " + cfupFilePath);
        return false;
    }

    auto read_exact = [&](void* buf, size_t n) -> bool {
        in.read(static_cast<char*>(buf), n);
        return in.gcount() == static_cast<std::streamsize>(n);
    };

    // ── Read CFUP v2 Header (16 bytes) ──
    uint8_t magic[4];
    if (!read_exact(magic, 4) || memcmp(magic, "CFUP", 4) != 0) {
        Logger::Log(LOG_ERROR, "Not a valid CFUP v2 file");
        return false;
    }

    uint8_t b2[2], b4[4];
    if (!read_exact(b2, 2)) return false;
    uint16_t version = r_u16(b2);
    if (version != 2) {
        Logger::Log(LOG_ERROR, "Unsupported CFUP version: " + std::to_string(version));
        return false;
    }
    
    if (!read_exact(b2, 2)) return false; // flags
    if (!read_exact(b4, 4)) return false; // reserved
    
    uint32_t file_count = r_u32(b4);

    try {
        int error = 0;
        zip_t* zip = zip_open(zipFilePath.c_str(), ZIP_CREATE | ZIP_TRUNCATE, &error);
        if (!zip) {
            zip_error_t zip_error;
            zip_error_init_with_code(&zip_error, error);
            Logger::Log(LOG_ERROR, "Failed to create ZIP: " + std::string(zip_error_strerror(&zip_error)));
            return false;
        }

        uint8_t b8[8];

        for (uint32_t i = 0; i < file_count; ++i) {
            if (!read_exact(b4, 4)) return false;
            uint32_t path_len = r_u32(b4);

            std::string relative_path(path_len, '\0');
            if (!read_exact(relative_path.data(), path_len)) return false;

            if (!read_exact(b8, 8)) return false;
            uint64_t data_size = r_u64(b8);

            // Stream file data to ZIP using callback
            CFUPSourceData* src_data = new CFUPSourceData(&in, data_size);
            
            zip_source_t* source = zip_source_function(zip, cfup_source_callback, src_data);
            if (!source) {
                Logger::Log(LOG_ERROR, "Failed to create ZIP source for: " + relative_path);
                delete src_data;
                zip_close(zip);
                return false;
            }

            zip_int64_t idx = zip_file_add(zip, relative_path.c_str(), source, ZIP_FL_OVERWRITE | ZIP_FL_ENC_UTF_8);
            if (idx < 0) {
                Logger::Log(LOG_ERROR, "Failed to add file to ZIP: " + relative_path);
                zip_source_free(source);
                zip_close(zip);
                return false;
            }

            zip_set_file_compression(zip, idx, ZIP_CM_DEFLATE, 6);

            // CRITICAL: Skip the 8-byte XXH64 hash that follows the data in v2!
            if (!read_exact(b8, 8)) return false;
        }

        // We are now at the TOC. We don't need it for ZIP conversion, so just close.
        if (zip_close(zip) != 0) {
            Logger::Log(LOG_ERROR, "Failed to finalize ZIP file");
            return false;
        }

        in.close();
        Logger::Log(LOG_INFO, "Converted CFUP v2 to ZIP: " + std::to_string(file_count) + " files");
        return true;

    } catch (const std::exception& e) {
        Logger::Log(LOG_ERROR, "Exception during CFUP to ZIP conversion: " + std::string(e.what()));
        in.close();
        return false;
    }
}

#endif // ZIP_TO_CFUP_CONVERTER_H