#include "folder_packer.h"
#define XXH_INLINE_ALL
#include "../../utils/hashers/xxhash.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <cstdio>
#include <set>

namespace fs = std::filesystem;

// ═════════════════════════════════════════════════════════════════
//  CFUP v2 format
//  ─────────────
//  All integers little-endian.
//
//  Header  (16 bytes)
//    0  magic[4]       "CFUP"
//    4  version        u16  (=2)
//    6  flags          u16  (bit0: follow_symlinks)
//    8  file_count     u32
//   12  reserved       u32  (=0)
//
//  File Entries  (file_count, sequential)
//    u32 path_len
//    char  path[path_len]   UTF-8, '/' separators, relative
//    u64   data_size
//    bytes data[data_size]
//    u64   xxh64             (XXH64 of data)
//
//  TOC  (file_count entries)
//    u32   path_len
//    char  path[path_len]
//    u64   data_offset        (from start of file)
//    u64   data_size
//    u64   xxh64
//
//  Footer  (20 bytes) 
//    0  magic[4]       "CFUP"
//    4  version        u16
//    6  flags          u16
//    8  file_count     u32
//   12  toc_offset     u64
// ═════════════════════════════════════════════════════════════════

static constexpr uint8_t  CFUP_MAGIC[4]   = {'C','F','U','P'};
static constexpr uint16_t CFUP_VERSION     = 2;
static constexpr uint16_t CFUP_FLAG_FOLLOW_SYMLINKS = 0x0001;
static constexpr size_t   CFUP_HEADER_SIZE = 16;
static constexpr size_t   CFUP_FOOTER_SIZE = 20;
static constexpr size_t   BUF_SZ           = 4 * 1024 * 1024;

// ─────────────────────────────────────────────────────────────────
//  Little-endian encode
// ─────────────────────────────────────────────────────────────────

static bool w_u16(WriteFn& dst, uint16_t v) {
    uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) };
    return dst(b, 2);
}
static bool w_u32(WriteFn& dst, uint32_t v) {
    uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8),
                     (uint8_t)(v >> 16), (uint8_t)(v >> 24) };
    return dst(b, 4);
}
static bool w_u64(WriteFn& dst, uint64_t v) {
    uint8_t b[8];
    for (int i = 0; i < 8; ++i) b[i] = (uint8_t)(v >> (i * 8));
    return dst(b, 8);
}
static bool w_bytes(WriteFn& dst, const void* p, size_t n) {
    return dst(p, n);
}

// ─────────────────────────────────────────────────────────────────
//  Little-endian decode
// ─────────────────────────────────────────────────────────────────

static uint16_t r_u16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t r_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t r_u64(const uint8_t* p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (i * 8);
    return v;
}

// ─────────────────────────────────────────────────────────────────
//  Path safety - blocks directory traversal
// ─────────────────────────────────────────────────────────────────

static bool is_safe_relative_path(const std::string& rel) {
    if (rel.empty()) return false;
    if (rel[0] == '/' || rel[0] == '\\') return false;
    if (rel.size() >= 2 && rel[1] == ':') return false;
    if (rel.size() >= 2 && rel[0] == '\\' && rel[1] == '\\') return false;
    if (rel.find('\0') != std::string::npos) return false;

    for (const auto& part : fs::path(rel)) {
        std::string s = part.string();
        if (s == ".." || s.empty()) return false;
    }
    return true;
}

static bool path_is_within(const fs::path& target, const fs::path& root) {
    std::error_code ec;
    auto ct = fs::weakly_canonical(target, ec);
    if (ec) return false;
    auto cr = fs::weakly_canonical(root, ec);
    if (ec) return false;

    std::string ts = ct.string();
    std::string rs = cr.string();
    if (ts == rs) return true;
    if (ts.size() > rs.size() &&
        ts.compare(0, rs.size(), rs) == 0 &&
        (ts[rs.size()] == '/' || ts[rs.size()] == '\\'))
        return true;
    return false;
}

// ─────────────────────────────────────────────────────────────────
//  File collection
//
//  Each entry is { physical_path, logical_path } where:
//    physical_path — the real path used to open and read the file
//                    (follows symlinks to find actual bytes)
//    logical_path  — the path that will be stored in the archive
//                    (preserves symlink names, relative to pack root)
//
//  This separation is the core fix: we no longer call fs::relative()
//  on the physical path, which would break when a symlink target
//  lives outside the canonical root.
// ─────────────────────────────────────────────────────────────────

using FileEntry = std::pair<fs::path, fs::path>; // {physical, logical}

static void collect_files_recursive(
    const fs::path& physical_dir,
    const fs::path& logical_dir,
    std::vector<FileEntry>& out_paths,
    bool follow_symlinks)
{
    std::error_code ec;
    for (auto it = fs::directory_iterator(physical_dir,
             fs::directory_options::skip_permission_denied, ec);
         it != fs::directory_iterator(); it.increment(ec))
    {
        if (ec) { ec.clear(); continue; }

        const auto& entry = *it;

        bool is_sym  = entry.is_symlink(ec);  if (ec) { ec.clear(); continue; }
        bool is_file = entry.is_regular_file(ec); if (ec) { ec.clear(); continue; }
        bool is_dir  = entry.is_directory(ec);    if (ec) { ec.clear(); continue; }

        if (is_sym && !follow_symlinks) continue;

        // logical name always uses the symlink's own filename,
        // never the target's — this is what materializes the link
        // in place rather than following the redirect in the archive.
        fs::path logical_entry = logical_dir / entry.path().filename();

        if (is_file) {
            // physical = entry.path() — the OS will resolve the symlink
            // when we open it for reading, which is exactly what we want.
            out_paths.push_back({ entry.path(), logical_entry });
        } else if (is_dir) {
            fs::path physical_target;
            if (is_sym) {
                // Resolve the symlink one level so we recurse into the
                // real directory, but keep logical_entry as the link name.
                physical_target = fs::read_symlink(entry.path(), ec);
                if (ec) { ec.clear(); continue; }
                // Make absolute if target was a relative symlink
                if (physical_target.is_relative())
                    physical_target = entry.path().parent_path() / physical_target;
            } else {
                physical_target = entry.path();
            }
            collect_files_recursive(physical_target, logical_entry,
                                    out_paths, follow_symlinks);
        }
    }
}

static bool collect_files(const std::string& folderPath,
                          std::vector<FileEntry>& out_paths,
                          bool follow_symlinks)
{
    fs::path folder(folderPath);
    if (!fs::exists(folder) || !fs::is_directory(folder)) {
        Logger::Log(LOG_ERROR, "Input is not a valid folder: " + folderPath);
        return false;
    }

    fs::path canonical_folder = fs::canonical(folder);

    // logical root is empty — all logical paths will be relative to it
    collect_files_recursive(canonical_folder, fs::path{},
                            out_paths, follow_symlinks);

    std::sort(out_paths.begin(), out_paths.end(),
              [](const FileEntry& a, const FileEntry& b){
                  return a.second < b.second; // sort by logical path
              });
    return true;
}

// ═════════════════════════════════════════════════════════════════
//  pack_folder_stream
// ═════════════════════════════════════════════════════════════════

bool pack_folder_stream(const std::string& folderPath, WriteFn dst,
                        bool follow_symlinks)
{
    std::vector<FileEntry> file_entries;
    if (!collect_files(folderPath, file_entries, follow_symlinks))
        return false;

    const uint32_t file_count = static_cast<uint32_t>(file_entries.size());
    const uint16_t flags = follow_symlinks ? CFUP_FLAG_FOLLOW_SYMLINKS : 0;

    if (!w_bytes(dst, CFUP_MAGIC, 4) ||
        !w_u16(dst, CFUP_VERSION)   ||
        !w_u16(dst, flags)          ||
        !w_u32(dst, file_count)     ||
        !w_u32(dst, 0))
    {
        Logger::Log(LOG_ERROR, "Write error (header)");
        return false;
    }

    uint64_t cur = CFUP_HEADER_SIZE;

    struct TOCEntry {
        std::string path;
        uint64_t data_offset;
        uint64_t data_size;
        uint64_t xxh64;
    };
    std::vector<TOCEntry> toc;
    toc.reserve(file_count);

    std::vector<char> buf(BUF_SZ);

    for (const auto& [physical_path, logical_path] : file_entries) {
        // Use the pre-computed logical path directly — no fs::relative() needed.
        // This is safe because logical_path was built by concatenating
        // symlink filenames, never by resolving symlink targets.
        std::string rel_str = logical_path.generic_string();

        if (!is_safe_relative_path(rel_str)) {
            Logger::Log(LOG_ERROR, "Unsafe logical path, skipping: " + rel_str);
            continue;
        }

        uint32_t path_len = static_cast<uint32_t>(rel_str.size());

        // Open via physical path — kernel follows the symlink chain here
        std::ifstream in(physical_path, std::ios::binary | std::ios::ate);
        if (!in) {
            Logger::Log(LOG_ERROR, "Cannot open: " + physical_path.string());
            return false;
        }
        uint64_t data_size = static_cast<uint64_t>(in.tellg());
        in.seekg(0);

        uint64_t data_offset = cur + 4 + path_len + 8;

        if (!w_u32(dst, path_len) ||
            !w_bytes(dst, rel_str.data(), path_len) ||
            !w_u64(dst, data_size))
        {
            Logger::Log(LOG_ERROR, "Write error (entry): " + rel_str);
            return false;
        }

        XXH64_state_t hash_state;
        if (XXH64_reset(&hash_state, 0) != XXH_OK) {
            Logger::Log(LOG_ERROR, "XXH64 reset failed: " + rel_str);
            return false;
        }

        uint64_t left = data_size;
        while (left > 0) {
            size_t to_read = static_cast<size_t>(std::min<uint64_t>(left, BUF_SZ));
            in.read(buf.data(), static_cast<std::streamsize>(to_read));
            if (static_cast<size_t>(in.gcount()) != to_read) {
                Logger::Log(LOG_ERROR, "Read error: " + physical_path.string());
                return false;
            }
            if (XXH64_update(&hash_state, buf.data(), to_read) != XXH_OK) {
                Logger::Log(LOG_ERROR, "XXH64 update failed: " + rel_str);
                return false;
            }
            if (!w_bytes(dst, buf.data(), to_read)) {
                Logger::Log(LOG_ERROR, "Write error (data): " + rel_str);
                return false;
            }
            left -= to_read;
        }

        uint64_t hash = XXH64_digest(&hash_state);

        if (!w_u64(dst, hash)) {
            Logger::Log(LOG_ERROR, "Write error (hash): " + rel_str);
            return false;
        }

        cur = data_offset + data_size + 8;
        toc.push_back({ rel_str, data_offset, data_size, hash });
    }

    // ── TOC ──
    uint64_t toc_offset = cur;
    for (const auto& e : toc) {
        uint32_t plen = static_cast<uint32_t>(e.path.size());
        if (!w_u32(dst, plen) ||
            !w_bytes(dst, e.path.data(), plen) ||
            !w_u64(dst, e.data_offset) ||
            !w_u64(dst, e.data_size) ||
            !w_u64(dst, e.xxh64))
        {
            Logger::Log(LOG_ERROR, "Write error (TOC)");
            return false;
        }
    }

    // ── Footer ──
    if (!w_bytes(dst, CFUP_MAGIC, 4) ||
        !w_u16(dst, CFUP_VERSION) ||
        !w_u16(dst, flags)        ||
        !w_u32(dst, file_count)   ||
        !w_u64(dst, toc_offset))
    {
        Logger::Log(LOG_ERROR, "Write error (footer)");
        return false;
    }

    Logger::Log(LOG_INFO, "Packed " + std::to_string(file_count) +
                 " files (CFUP v2 XXH64, " + std::to_string(toc_offset) + " bytes data)");
    return true;
}

// ═════════════════════════════════════════════════════════════════
//  unpack_stream
// ═════════════════════════════════════════════════════════════════

bool unpack_stream(ReadFn src, const std::string& outputFolderPath,
                   bool verify)
{
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
    auto read_u16 = [&](uint16_t& v) -> bool {
        uint8_t b[2]; if (!read_exact(b, 2)) return false; v = r_u16(b); return true;
    };
    auto read_u32 = [&](uint32_t& v) -> bool {
        uint8_t b[4]; if (!read_exact(b, 4)) return false; v = r_u32(b); return true;
    };
    auto read_u64 = [&](uint64_t& v) -> bool {
        uint8_t b[8]; if (!read_exact(b, 8)) return false; v = r_u64(b); return true;
    };

    uint8_t magic[4];
    if (!read_exact(magic, 4) || memcmp(magic, CFUP_MAGIC, 4) != 0) {
        Logger::Log(LOG_ERROR, "Not a CFUP file (bad magic)");
        return false;
    }
    uint16_t version, flags;
    uint32_t file_count, reserved;
    if (!read_u16(version) || !read_u16(flags) ||
        !read_u32(file_count) || !read_u32(reserved))
    {
        Logger::Log(LOG_ERROR, "Cannot read header");
        return false;
    }
    if (version != CFUP_VERSION) {
        Logger::Log(LOG_ERROR, "Unsupported CFUP version: " + std::to_string(version));
        return false;
    }

    fs::path out_root(outputFolderPath);
    fs::create_directories(out_root);
    std::error_code ec;
    fs::path canon_root = fs::weakly_canonical(out_root, ec);
    if (ec) {
        Logger::Log(LOG_ERROR, "Cannot resolve output path: " + outputFolderPath);
        return false;
    }

    std::vector<char> buf(BUF_SZ);
    XXH64_state_t hash_state; // Stack-allocated

    for (uint32_t i = 0; i < file_count; ++i) {
        uint32_t path_len;
        if (!read_u32(path_len)) {
            Logger::Log(LOG_ERROR, "Cannot read path length (entry " + std::to_string(i) + ")");
            return false;
        }
        if (path_len > 65535) {
            Logger::Log(LOG_ERROR, "Path length too large (entry " + std::to_string(i) + ")");
            return false;
        }

        std::string rel_path(path_len, '\0');
        if (!read_exact(rel_path.data(), path_len)) {
            Logger::Log(LOG_ERROR, "Cannot read path string (entry " + std::to_string(i) + ")");
            return false;
        }

        if (!is_safe_relative_path(rel_path)) {
            Logger::Log(LOG_ERROR, "Unsafe path detected in archive: " + rel_path);
            return false;
        }

        uint64_t data_size;
        if (!read_u64(data_size)) {
            Logger::Log(LOG_ERROR, "Cannot read data size (entry " + std::to_string(i) + ")");
            return false;
        }

        fs::path out_path = canon_root / rel_path;
        if (!path_is_within(out_path, canon_root)) {
            Logger::Log(LOG_ERROR, "Path traversal attempt blocked: " + rel_path);
            return false;
        }

        fs::create_directories(out_path.parent_path());

        std::ofstream out_file(out_path, std::ios::binary);
        if (!out_file) {
            Logger::Log(LOG_ERROR, "Cannot create: " + out_path.string());
            return false;
        }

        if (verify) {
            if (XXH64_reset(&hash_state, 0) != XXH_OK) {
                Logger::Log(LOG_ERROR, "XXH64 reset failed");
                return false;
            }
        }

        uint64_t left = data_size;
        while (left > 0) {
            size_t to_read = static_cast<size_t>(std::min<uint64_t>(left, BUF_SZ));
            if (!read_exact(buf.data(), to_read)) {
                Logger::Log(LOG_ERROR, "Short read for: " + rel_path);
                return false;
            }
            if (verify) {
                if (XXH64_update(&hash_state, buf.data(), to_read) != XXH_OK) {
                    Logger::Log(LOG_ERROR, "XXH64 update failed");
                    return false;
                }
            }
            out_file.write(buf.data(), to_read);
            if (!out_file) {
                Logger::Log(LOG_ERROR, "Write error: " + out_path.string());
                return false;
            }
            left -= to_read;
        }

        uint64_t computed_hash = 0;
        if (verify) {
            computed_hash = XXH64_digest(&hash_state);
        }

        uint64_t stored_hash;
        if (!read_u64(stored_hash)) {
            Logger::Log(LOG_ERROR, "Cannot read hash for: " + rel_path);
            return false;
        }

        if (verify && stored_hash != computed_hash) {
            Logger::Log(LOG_ERROR, "XXH64 mismatch for: " + rel_path);
            return false;
        }
    }

    Logger::Log(LOG_INFO, "Unpacked " + std::to_string(file_count) + " files (stream) to " + outputFolderPath);
    return true;
}

// ═════════════════════════════════════════════════════════════════
//  list_packed_files  — read TOC for random access
// ═════════════════════════════════════════════════════════════════

bool list_packed_files(const std::string& packedFilePath,
                       std::vector<PackedFileEntry>& out_entries)
{
    std::ifstream f(packedFilePath, std::ios::binary | std::ios::ate);
    if (!f) {
        Logger::Log(LOG_ERROR, "Cannot open: " + packedFilePath);
        return false;
    }

    auto read_exact = [&](void* buf, size_t n) -> bool {
        f.read(static_cast<char*>(buf), n);
        return static_cast<size_t>(f.gcount()) == n;
    };

    uint64_t file_size = static_cast<uint64_t>(f.tellg());
    if (file_size < CFUP_HEADER_SIZE + CFUP_FOOTER_SIZE) {
        Logger::Log(LOG_ERROR, "File too small to be valid CFUP: " + packedFilePath);
        return false;
    }

    f.seekg(static_cast<std::streamoff>(file_size - CFUP_FOOTER_SIZE), std::ios::beg);

    uint8_t magic[4];
    if (!read_exact(magic, 4) || memcmp(magic, CFUP_MAGIC, 4) != 0) {
        Logger::Log(LOG_ERROR, "Bad footer magic: " + packedFilePath);
        return false;
    }

    uint8_t b2[2], b4[4], b8[8];

    if (!read_exact(b2, 2)) return false;
    uint16_t version = r_u16(b2);

    if (!read_exact(b2, 2)) return false; // flags

    if (!read_exact(b4, 4)) return false;
    uint32_t file_count = r_u32(b4);

    if (!read_exact(b8, 8)) return false;
    uint64_t toc_offset = r_u64(b8);

    if (version != CFUP_VERSION) {
        Logger::Log(LOG_ERROR, "Unsupported CFUP version: " + std::to_string(version));
        return false;
    }

    f.seekg(static_cast<std::streamoff>(toc_offset), std::ios::beg);

    out_entries.clear();
    out_entries.reserve(file_count);

    for (uint32_t i = 0; i < file_count; ++i) {
        if (!read_exact(b4, 4)) return false;
        uint32_t path_len = r_u32(b4);
        if (path_len > 65535) return false;

        std::string path(path_len, '\0');
        if (!read_exact(path.data(), path_len)) return false;

        if (!read_exact(b8, 8)) return false;
        uint64_t data_offset = r_u64(b8);

        if (!read_exact(b8, 8)) return false;
        uint64_t data_size = r_u64(b8);

        if (!read_exact(b8, 8)) return false;
        uint64_t xxh64 = r_u64(b8);

        out_entries.push_back({ std::move(path), data_offset, data_size, xxh64 });
    }

    return true;
}

// ═════════════════════════════════════════════════════════════════
//  extract_file  — extract a single file by index using TOC
// ═════════════════════════════════════════════════════════════════

bool extract_file(const std::string& packedFilePath,
                  uint32_t file_index,
                  const std::string& outputPath,
                  bool verify)
{
    std::vector<PackedFileEntry> entries;
    if (!list_packed_files(packedFilePath, entries)) return false;

    if (file_index >= entries.size()) {
        Logger::Log(LOG_ERROR, "File index out of bounds: " + std::to_string(file_index));
        return false;
    }

    const auto& e = entries[file_index];

    if (!is_safe_relative_path(e.path)) {
        Logger::Log(LOG_ERROR, "Unsafe path in archive: " + e.path);
        return false;
    }

    fs::path out_root(outputPath);
    fs::path out_path = out_root / e.path;

    if (!path_is_within(out_path, out_root)) {
        Logger::Log(LOG_ERROR, "Path traversal attempt blocked: " + e.path);
        return false;
    }

    fs::create_directories(out_path.parent_path());

    std::ifstream in(packedFilePath, std::ios::binary);
    if (!in) {
        Logger::Log(LOG_ERROR, "Cannot open: " + packedFilePath);
        return false;
    }

    in.seekg(static_cast<std::streamoff>(e.offset), std::ios::beg);

    std::ofstream out(out_path, std::ios::binary);
    if (!out) {
        Logger::Log(LOG_ERROR, "Cannot create: " + out_path.string());
        return false;
    }

    XXH64_state_t hash_state; // Stack-allocated
    if (verify) {
        if (XXH64_reset(&hash_state, 0) != XXH_OK) {
            return false;
        }
    }

    uint64_t left = e.size;
    std::vector<char> buf(BUF_SZ);

    while (left > 0) {
        size_t to_read = static_cast<size_t>(std::min<uint64_t>(left, BUF_SZ));
        in.read(buf.data(), to_read);
        if (static_cast<size_t>(in.gcount()) != to_read) {
            Logger::Log(LOG_ERROR, "Short read for: " + e.path);
            return false;
        }
        if (verify) {
            if (XXH64_update(&hash_state, buf.data(), to_read) != XXH_OK) {
                return false;
            }
        }
        out.write(buf.data(), to_read);
        if (!out) {
            Logger::Log(LOG_ERROR, "Write error: " + out_path.string());
            return false;
        }
        left -= to_read;
    }

    if (verify) {
        uint64_t computed_hash = XXH64_digest(&hash_state);
        if (computed_hash != e.xxh64) {
            Logger::Log(LOG_ERROR, "XXH64 mismatch for: " + e.path);
            return false;
        }
    }

    return true;
}

// ═════════════════════════════════════════════════════════════════
//  CFUP heuristic probe
// ═════════════════════════════════════════════════════════════════

bool looks_like_cfup_header(const uint8_t* bytes, size_t len) {
    if (len < 4) return false;
    return memcmp(bytes, CFUP_MAGIC, 4) == 0;
}

bool looks_like_cfup(const std::string& filePath) {
    std::ifstream f(filePath, std::ios::binary);
    if (!f) return false;
    uint8_t buf[4];
    f.read(reinterpret_cast<char*>(buf), 4);
    if (f.gcount() < 4) return false;
    return looks_like_cfup_header(buf, 4);
}

// ═════════════════════════════════════════════════════════════════
//  Original file-based API — thin wrappers around the stream API
// ═════════════════════════════════════════════════════════════════

bool pack_folder(const std::string& folderPath,
                 const std::string& packedFilePath,
                 bool follow_symlinks)
{
    std::ofstream out(packedFilePath, std::ios::binary);
    if (!out) {
        Logger::Log(LOG_ERROR, "Cannot create: " + packedFilePath);
        return false;
    }
    WriteFn dst = [&out](const void* buf, size_t len) -> bool {
        out.write(static_cast<const char*>(buf), len);
        return out.good();
    };
    bool ok = pack_folder_stream(folderPath, dst, follow_symlinks);
    out.close();
    if (!ok) {
        fs::remove(packedFilePath);
    }
    return ok;
}

bool unpack_packed_file(const std::string& packedFilePath,
                        const std::string& outputFolderPath,
                        bool verify)
{
    FILE* f = fopen(packedFilePath.c_str(), "rb");
    if (!f) {
        Logger::Log(LOG_ERROR, "Cannot open: " + packedFilePath);
        return false;
    }
    ReadFn src = [f](void* buf, size_t len) -> size_t {
        return fread(buf, 1, len, f);
    };
    bool ok = unpack_stream(src, outputFolderPath, verify);
    fclose(f);
    return ok;
}