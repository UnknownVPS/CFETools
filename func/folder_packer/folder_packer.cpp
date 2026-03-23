#include "folder_packer.h"
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────────
//  Shared helpers
// ─────────────────────────────────────────────────────────────────

static bool collect_files(const std::string& folderPath,
                          std::vector<fs::path>& out_paths,
                          fs::path& out_canonical) {
    fs::path folder(folderPath);
    if (!fs::exists(folder) || !fs::is_directory(folder)) {
        Logger::Log(LOG_ERROR, "Input is not a valid folder: " + folderPath);
        return false;
    }
    out_canonical = fs::canonical(folder);
    for (const auto& p : fs::recursive_directory_iterator(out_canonical))
        if (fs::is_regular_file(p)) out_paths.push_back(p.path());
    std::sort(out_paths.begin(), out_paths.end());
    return true;
}

// Write a uint32_t in native byte order via WriteFn
static bool write_u32(WriteFn& dst, uint32_t v) {
    return dst(&v, sizeof(v));
}
// Write a uint64_t in native byte order via WriteFn
static bool write_u64(WriteFn& dst, uint64_t v) {
    return dst(&v, sizeof(v));
}

// ─────────────────────────────────────────────────────────────────
//  pack_folder_stream  — write cfup bytes to dst WriteFn
// ─────────────────────────────────────────────────────────────────

bool pack_folder_stream(const std::string& folderPath, WriteFn dst) {
    std::vector<fs::path> file_paths;
    fs::path canonical_folder;
    if (!collect_files(folderPath, file_paths, canonical_folder)) return false;

    const uint32_t file_count = static_cast<uint32_t>(file_paths.size());
    if (!write_u32(dst, file_count)) { Logger::Log(LOG_ERROR, "Write error (file count)"); return false; }

    constexpr size_t BUF_SZ = 1024 * 1024 * 4; // 4 MB
    std::vector<char> buffer(BUF_SZ);

    for (const fs::path& fp : file_paths) {
        fs::path relative     = fs::relative(fp, canonical_folder);
        std::string rel_str   = relative.generic_string();
        uint32_t path_len     = static_cast<uint32_t>(rel_str.size());

        if (!write_u32(dst, path_len)) goto write_err;
        if (!dst(rel_str.data(), path_len)) goto write_err;

        {
            std::ifstream in(fp, std::ios::binary | std::ios::ate);
            if (!in) { Logger::Log(LOG_ERROR, "Cannot open: " + fp.string()); return false; }
            std::streamsize file_size = in.tellg();
            in.seekg(0);

            uint64_t data_size = static_cast<uint64_t>(file_size);
            if (!write_u64(dst, data_size)) goto write_err;

            std::streamsize left = file_size;
            while (left > 0) {
                std::streamsize to_read = std::min(left, static_cast<std::streamsize>(BUF_SZ));
                if (!in.read(buffer.data(), to_read)) {
                    Logger::Log(LOG_ERROR, "Read error: " + fp.string()); return false;
                }
                if (!dst(buffer.data(), to_read)) goto write_err;
                left -= to_read;
            }
        }
        continue;
write_err:
        Logger::Log(LOG_ERROR, "Write error while packing"); return false;
    }

    Logger::Log(LOG_INFO, "Packed " + std::to_string(file_count) + " files (stream)");
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  unpack_stream  — read cfup bytes from src ReadFn
// ─────────────────────────────────────────────────────────────────

bool unpack_stream(ReadFn src, const std::string& outputFolderPath) {
    // Helper: read exactly n bytes or return false
    auto read_exact = [&](void* buf, size_t n) -> bool {
        size_t got = 0;
        uint8_t* p = static_cast<uint8_t*>(buf);
        while (got < n) {
            size_t r = src(p + got, n - got);
            if (r == 0) return false;
            got += r;
        }
        return true;
    };

    uint32_t file_count = 0;
    if (!read_exact(&file_count, sizeof(file_count))) {
        Logger::Log(LOG_ERROR, "Cannot read file count"); return false;
    }

    fs::path out_root(outputFolderPath);
    constexpr size_t BUF_SZ = 1024 * 1024 * 4;
    std::vector<char> buffer(BUF_SZ);

    for (uint32_t i = 0; i < file_count; ++i) {
        uint32_t path_len = 0;
        if (!read_exact(&path_len, sizeof(path_len))) {
            Logger::Log(LOG_ERROR, "Cannot read path length (entry " + std::to_string(i) + ")");
            return false;
        }
        std::string rel_path(path_len, '\0');
        if (!read_exact(rel_path.data(), path_len)) {
            Logger::Log(LOG_ERROR, "Cannot read path string (entry " + std::to_string(i) + ")");
            return false;
        }
        uint64_t data_size = 0;
        if (!read_exact(&data_size, sizeof(data_size))) {
            Logger::Log(LOG_ERROR, "Cannot read data size (entry " + std::to_string(i) + ")");
            return false;
        }

        fs::path out_path = out_root / rel_path;
        fs::create_directories(out_path.parent_path());

        std::ofstream out_file(out_path, std::ios::binary);
        if (!out_file) {
            Logger::Log(LOG_ERROR, "Cannot create: " + out_path.string()); return false;
        }

        uint64_t left = data_size;
        while (left > 0) {
            size_t to_read = static_cast<size_t>(std::min<uint64_t>(left, BUF_SZ));
            if (!read_exact(buffer.data(), to_read)) {
                Logger::Log(LOG_ERROR, "Short read for: " + rel_path); return false;
            }
            out_file.write(buffer.data(), to_read);
            if (!out_file) { Logger::Log(LOG_ERROR, "Write error: " + out_path.string()); return false; }
            left -= to_read;
        }
    }

    Logger::Log(LOG_INFO, "Unpacked " + std::to_string(file_count) + " files (stream) to " + outputFolderPath);
    return true;
}

// ─────────────────────────────────────────────────────────────────
//  CFUP heuristic probe
//  A cfup file starts with uint32_t file_count (LE), then for each file:
//    uint32_t path_len, char path[path_len], uint64_t data_size, ...
//  We just check that file_count is sane (< 1M) and path_len is plausible.
// ─────────────────────────────────────────────────────────────────

bool looks_like_cfup_header(const uint8_t* bytes, size_t len) {
    if (len < 8) return false;
    uint32_t file_count = 0;
    memcpy(&file_count, bytes, 4);
    if (file_count == 0 || file_count > 1'000'000) return false;
    // First path_len should be a reasonable filename length (< 4096)
    uint32_t first_path_len = 0;
    memcpy(&first_path_len, bytes + 4, 4);
    return first_path_len > 0 && first_path_len < 4096;
}

bool looks_like_cfup(const std::string& filePath) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f) return false;
    uint8_t buf[8] = {};
    f.read(reinterpret_cast<char*>(buf), 8);
    if (f.gcount() < 8) return false;
    return looks_like_cfup_header(buf, 8);
}

// ─────────────────────────────────────────────────────────────────
//  Original file-based API — thin wrappers around the stream API
// ─────────────────────────────────────────────────────────────────

bool pack_folder(const std::string& folderPath, const std::string& packedFilePath) {
    std::ofstream out(packedFilePath, std::ios::binary);
    if (!out) { Logger::Log(LOG_ERROR, "Cannot create: " + packedFilePath); return false; }
    WriteFn dst = [&out](const void* buf, size_t len) -> bool {
        out.write(static_cast<const char*>(buf), len);
        return out.good();
    };
    bool ok = pack_folder_stream(folderPath, dst);
    out.close();
    if (!ok) fs::remove(packedFilePath);
    return ok;
}

bool unpack_packed_file(const std::string& packedFilePath, const std::string& outputFolderPath) {
    FILE* f = fopen(packedFilePath.c_str(), "rb");
    if (!f) { Logger::Log(LOG_ERROR, "Cannot open: " + packedFilePath); return false; }
    ReadFn src = [f](void* buf, size_t len) -> size_t { return fread(buf, 1, len, f); };
    bool ok = unpack_stream(src, outputFolderPath);
    fclose(f);
    return ok;
}