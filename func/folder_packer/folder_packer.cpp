#include "folder_packer.h"
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <unordered_set>
#include <algorithm>
#include <cstring>

namespace fs = std::filesystem;

// Ultra-fast configuration - maximum performance settings
constexpr size_t ULTRA_BUFFER_SIZE = 128 * 1024 * 1024;    // 128MB buffer
constexpr size_t MMAP_THRESHOLD = 16 * 1024 * 1024;        // 16MB threshold
constexpr size_t BATCH_SIZE = 1000;                        // Process in batches
constexpr size_t METADATA_BUFFER = 32 * 1024 * 1024;       // 32MB metadata buffer

// FIXED: Properly movable and assignable struct
struct UltraFileInfo {
    fs::path path;
    std::string relative_path;
    uint64_t size;
    uint32_t path_len;
    bool use_mmap;

    // Default constructor
    UltraFileInfo() = default;

    // Move constructor
    UltraFileInfo(UltraFileInfo&& other) noexcept 
        : path(std::move(other.path))
        , relative_path(std::move(other.relative_path))
        , size(other.size)
        , path_len(other.path_len)
        , use_mmap(other.use_mmap) {}

    // Move assignment operator
    UltraFileInfo& operator=(UltraFileInfo&& other) noexcept {
        if (this != &other) {
            path = std::move(other.path);
            relative_path = std::move(other.relative_path);
            size = other.size;
            path_len = other.path_len;
            use_mmap = other.use_mmap;
        }
        return *this;
    }

    // Copy constructor (needed for sorting)
    UltraFileInfo(const UltraFileInfo& other) 
        : path(other.path)
        , relative_path(other.relative_path)
        , size(other.size)
        , path_len(other.path_len)
        , use_mmap(other.use_mmap) {}

    // Copy assignment operator (needed for sorting)
    UltraFileInfo& operator=(const UltraFileInfo& other) {
        if (this != &other) {
            path = other.path;
            relative_path = other.relative_path;
            size = other.size;
            path_len = other.path_len;
            use_mmap = other.use_mmap;
        }
        return *this;
    }
};

// Lock-free, zero-copy memory mapped file handler
class UltraFastMMap {
private:
    int fd = -1;
    void* mapped_data = MAP_FAILED;
    size_t file_size = 0;

public:
    __attribute__((always_inline)) inline bool map_file(const char* filepath) {
        fd = open(filepath, O_RDONLY);
        if (fd == -1) [[unlikely]] return false;

        struct stat st;
        if (fstat(fd, &st) == -1) [[unlikely]] {
            close(fd);
            return false;
        }

        file_size = st.st_size;
        mapped_data = mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE | MAP_POPULATE, fd, 0);

        if (mapped_data == MAP_FAILED) [[unlikely]] {
            close(fd);
            return false;
        }

        // Advise kernel for sequential access
        madvise(mapped_data, file_size, MADV_SEQUENTIAL | MADV_WILLNEED);
        return true;
    }

    __attribute__((always_inline)) inline const void* data() const noexcept { return mapped_data; }
    __attribute__((always_inline)) inline size_t size() const noexcept { return file_size; }

    ~UltraFastMMap() {
        if (mapped_data != MAP_FAILED) {
            munmap(mapped_data, file_size);
        }
        if (fd != -1) {
            close(fd);
        }
    }
};

// ULTRA-FAST folder packer - maximum performance, minimal system calls
bool pack_folder(const std::string& folderPath, const std::string& packedFilePath) {
    fs::path folder(folderPath);

    if (!fs::exists(folder) || !fs::is_directory(folder)) [[unlikely]] {
        Logger::Log(LOG_ERROR, "Invalid folder: " + folderPath);
        return false;
    }

    // PHASE 1: Lightning-fast file enumeration with pre-allocation
    std::vector<UltraFileInfo> file_infos;
    file_infos.reserve(10000); // Pre-allocate for common case

    uint64_t total_size = 0;
    size_t small_files = 0, large_files = 0;

    // Single-pass enumeration with immediate categorization
    for (const auto& entry : fs::recursive_directory_iterator(folder)) {
        if (!fs::is_regular_file(entry)) [[unlikely]] continue;

        std::error_code ec;
        auto file_size = fs::file_size(entry.path(), ec);
        if (ec) [[unlikely]] continue;

        UltraFileInfo info;
        info.path = entry.path();
        info.relative_path = fs::relative(entry.path(), folder).string();
        info.size = file_size;
        info.path_len = static_cast<uint32_t>(info.relative_path.size());
        info.use_mmap = file_size >= MMAP_THRESHOLD;

        total_size += file_size;
        if (info.use_mmap) large_files++; else small_files++;

        file_infos.emplace_back(std::move(info));
    }

    // PHASE 2: Optimal sorting for cache locality and batching
    std::sort(file_infos.begin(), file_infos.end(), 
              [](const UltraFileInfo& a, const UltraFileInfo& b) {
                  // Small files first, sorted by directory for locality
                  if (a.use_mmap != b.use_mmap) return !a.use_mmap;
                  if (!a.use_mmap) {
                      // Group small files by parent directory
                      auto a_parent = fs::path(a.relative_path).parent_path();
                      auto b_parent = fs::path(b.relative_path).parent_path();
                      if (a_parent != b_parent) return a_parent < b_parent;
                  }
                  return a.size < b.size; // Size order within same directory
              });

    // PHASE 3: Ultra-fast output setup with huge buffers
    int out_fd = open(packedFilePath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out_fd == -1) [[unlikely]] {
        Logger::Log(LOG_ERROR, "Failed to create: " + packedFilePath);
        return false;
    }

    // Pre-allocate file space to avoid filesystem overhead during writing
    if (ftruncate(out_fd, total_size + (file_infos.size() * 1024)) == -1) {
        // Non-fatal, continue without pre-allocation
    }

    // Use massive buffer for output to minimize system calls
    std::unique_ptr<char[]> output_buffer(new(std::nothrow) char[ULTRA_BUFFER_SIZE]);
    if (!output_buffer) [[unlikely]] {
        close(out_fd);
        return false;
    }

    FILE* out_file = fdopen(out_fd, "wb");
    if (!out_file) [[unlikely]] {
        close(out_fd);
        return false;
    }

    // Set buffer for maximum performance
    setvbuf(out_file, output_buffer.get(), _IOFBF, ULTRA_BUFFER_SIZE);

    const uint32_t file_count = static_cast<uint32_t>(file_infos.size());

    // PHASE 4: Batch write all metadata in one massive operation
    std::unique_ptr<char[]> metadata_buffer(new(std::nothrow) char[METADATA_BUFFER]);
    if (!metadata_buffer) [[unlikely]] {
        fclose(out_file);
        return false;
    }

    char* meta_ptr = metadata_buffer.get();

    // Write file count
    std::memcpy(meta_ptr, &file_count, sizeof(file_count));
    meta_ptr += sizeof(file_count);

    // Batch all metadata into single buffer
    for (const auto& info : file_infos) {
        std::memcpy(meta_ptr, &info.path_len, sizeof(info.path_len));
        meta_ptr += sizeof(info.path_len);

        std::memcpy(meta_ptr, info.relative_path.data(), info.path_len);
        meta_ptr += info.path_len;

        std::memcpy(meta_ptr, &info.size, sizeof(info.size));
        meta_ptr += sizeof(info.size);
    }

    // Single massive write for all metadata
    size_t metadata_size = meta_ptr - metadata_buffer.get();
    if (fwrite(metadata_buffer.get(), 1, metadata_size, out_file) != metadata_size) [[unlikely]] {
        fclose(out_file);
        return false;
    }

    // PHASE 5: Hyper-optimized file data processing
    std::unique_ptr<char[]> io_buffer(new(std::nothrow) char[ULTRA_BUFFER_SIZE]);
    if (!io_buffer) [[unlikely]] {
        fclose(out_file);
        return false;
    }

    // Process files with adaptive strategy
    for (const auto& info : file_infos) {
        if (info.use_mmap) {
            // Large files: Zero-copy memory mapping
            UltraFastMMap mmap_file;
            if (!mmap_file.map_file(info.path.c_str())) [[unlikely]] {
                fclose(out_file);
                return false;
            }

            // Stream from memory map in optimal chunks
            const char* data = static_cast<const char*>(mmap_file.data());
            size_t remaining = info.size;

            while (remaining > 0) {
                size_t chunk = std::min(remaining, ULTRA_BUFFER_SIZE);
                if (fwrite(data, 1, chunk, out_file) != chunk) [[unlikely]] {
                    fclose(out_file);
                    return false;
                }
                data += chunk;
                remaining -= chunk;
            }
        } else {
            // Small files: Optimized streaming with huge buffer
            int in_fd = open(info.path.c_str(), O_RDONLY);
            if (in_fd == -1) [[unlikely]] continue;

            size_t remaining = info.size;
            while (remaining > 0) {
                size_t to_read = std::min(remaining, ULTRA_BUFFER_SIZE);
                ssize_t bytes_read = read(in_fd, io_buffer.get(), to_read);

                if (bytes_read <= 0) [[unlikely]] break;

                if (fwrite(io_buffer.get(), 1, bytes_read, out_file) != static_cast<size_t>(bytes_read)) [[unlikely]] {
                    close(in_fd);
                    fclose(out_file);
                    return false;
                }

                remaining -= bytes_read;
            }
            close(in_fd);
        }
    }

    // Force all data to disk immediately
    fflush(out_file);
    fsync(fileno(out_file));
    fclose(out_file);

    Logger::Log(LOG_INFO, "ULTRA-FAST packed " + std::to_string(file_count) + 
                " files (" + std::to_string(total_size / (1024*1024)) + "MB)");
    return true;
}

// ULTRA-FAST unpacker with zero-copy and batch operations
bool unpack_packed_file(const std::string& packedFilePath, const std::string& outputFolderPath) {
    // Memory map the entire packed file for zero-copy access
    UltraFastMMap packed_file;
    if (!packed_file.map_file(packedFilePath.c_str())) {
        Logger::Log(LOG_ERROR, "Failed to map packed file: " + packedFilePath);
        return false;
    }

    const char* data = static_cast<const char*>(packed_file.data());
    size_t offset = 0;

    // Read file count
    uint32_t file_count;
    std::memcpy(&file_count, data, sizeof(file_count));
    offset += sizeof(file_count);

    // Pre-allocate all structures
    std::vector<std::string> paths;
    std::vector<uint64_t> sizes;
    std::vector<size_t> data_offsets;

    paths.reserve(file_count);
    sizes.reserve(file_count);
    data_offsets.reserve(file_count);

    // Parse all metadata in one pass
    for (uint32_t i = 0; i < file_count; ++i) {
        uint32_t path_len;
        std::memcpy(&path_len, data + offset, sizeof(path_len));
        offset += sizeof(path_len);

        paths.emplace_back(data + offset, path_len);
        offset += path_len;

        uint64_t file_size;
        std::memcpy(&file_size, data + offset, sizeof(file_size));
        offset += sizeof(file_size);

        sizes.push_back(file_size);
        data_offsets.push_back(offset);
        offset += file_size; // Skip to next file
    }

    // Batch create all directories
    fs::path output_base(outputFolderPath);
    std::unordered_set<std::string> created_dirs;
    created_dirs.reserve(file_count / 4); // Estimate

    for (const auto& rel_path : paths) {
        auto parent = (output_base / rel_path).parent_path();
        auto parent_str = parent.string();

        if (created_dirs.find(parent_str) == created_dirs.end()) {
            std::error_code ec;
            fs::create_directories(parent, ec);
            if (!ec) created_dirs.insert(std::move(parent_str));
        }
    }

    // Ultra-fast file writing with zero-copy
    for (uint32_t i = 0; i < file_count; ++i) {
        auto out_path = output_base / paths[i];

        int out_fd = open(out_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd == -1) [[unlikely]] continue;

        // Write entire file in one system call when possible
        const char* file_data = data + data_offsets[i];
        size_t file_size = sizes[i];

        while (file_size > 0) {
            ssize_t written = write(out_fd, file_data, file_size);
            if (written <= 0) [[unlikely]] break;

            file_data += written;
            file_size -= written;
        }

        close(out_fd);
    }

    Logger::Log(LOG_INFO, "ULTRA-FAST unpacked " + std::to_string(file_count) + " files");
    return true;
}