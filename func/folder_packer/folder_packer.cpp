#include "folder_packer.h"
#include <filesystem>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <iostream>
#include <algorithm>
namespace fs = std::filesystem;

// --- Constants ---
constexpr size_t BUFFER_SIZE = 1 * 1024 * 1024;    // 1 MB buffer size
constexpr size_t QUEUE_MAX_BUFFERS = 8;            // bounded queue depth
constexpr uint64_t SMALL_FILE_LIMIT = 256 * 1024;  // <= 256 KB = "small"
constexpr size_t SMALL_FILE_BATCH_SIZE = 2 * 1024 * 1024; // 2 MB

// --- File entry ---
struct FileEntry {
    fs::path full;
    std::string relative;
    uint64_t size;
    bool is_small_file() const { return size <= SMALL_FILE_LIMIT; }
};

// --- Chunk ---
struct FileChunk {
    enum Type { SMALL_FILE_BATCH, LARGE_FILE_CHUNK };
    uint32_t file_index;
    std::vector<char> data;
    Type type;
    std::vector<uint32_t> batch_file_indices;
    std::vector<size_t> batch_file_sizes;
};

// --- Thread-safe queue ---
class ChunkQueue {
public:
    void push(FileChunk&& chunk) {
        std::unique_lock<std::mutex> lk(m_);
        cv_full_.wait(lk, [&]{ return queue_.size() < max_capacity_ || closed_; });
        if (closed_) return;
        Logger::Log(LOG_DEBUG, "Queue push: chunk size=" + std::to_string(chunk.data.size()));
        queue_.push_back(std::move(chunk));
        lk.unlock();
        cv_empty_.notify_one();
    }
    bool pop(FileChunk& out) {
        std::unique_lock<std::mutex> lk(m_);
        cv_empty_.wait(lk, [&]{ return !queue_.empty() || closed_; });
        if (queue_.empty()) return false;
        out = std::move(queue_.front());
        Logger::Log(LOG_DEBUG, "Queue pop: chunk size=" + std::to_string(out.data.size()));
        queue_.pop_front();
        lk.unlock();
        cv_full_.notify_one();
        return true;
    }
    void close() {
        std::lock_guard<std::mutex> lk(m_);
        closed_ = true;
        cv_empty_.notify_all();
        cv_full_.notify_all();
    }
    void set_capacity(size_t c) { max_capacity_ = c; }
private:
    std::deque<FileChunk> queue_;
    std::mutex m_;
    std::condition_variable cv_empty_;
    std::condition_variable cv_full_;
    size_t max_capacity_ = QUEUE_MAX_BUFFERS;
    bool closed_ = false;
};

// --- Build file list ---
static std::vector<FileEntry> build_file_list(const fs::path& folder) {
    Logger::Log(LOG_DEBUG, "Scanning folder: " + folder.string());
    std::vector<FileEntry> files;
    for (auto const& p : fs::recursive_directory_iterator(folder)) {
        if (fs::is_regular_file(p.path())) {
            FileEntry e;
            e.full = p.path();
            e.relative = fs::relative(e.full, folder).string();
            e.size = static_cast<uint64_t>(fs::file_size(e.full));
            files.push_back(std::move(e));
        }
    }
    // small files first
    std::stable_sort(files.begin(), files.end(), [](const FileEntry& a, const FileEntry& b) {
        return a.is_small_file() && !b.is_small_file();
    });
    Logger::Log(LOG_DEBUG, "Found " + std::to_string(files.size()) + " files");
    return files;
}

// --- Small file batcher ---
class SmallFileBatcher {
private:
    std::vector<char> batch_buffer_;
    std::vector<uint32_t> file_indices_;
    std::vector<size_t> file_sizes_;
    size_t current_size_;
public:
    SmallFileBatcher() : current_size_(0) {
        batch_buffer_.reserve(SMALL_FILE_BATCH_SIZE);
    }
    bool add_file(uint32_t idx, const FileEntry& entry) {
        if (current_size_ + entry.size > SMALL_FILE_BATCH_SIZE && !file_indices_.empty()) {
            return false;
        }
        std::ifstream in(entry.full, std::ios::binary);
        if (!in) {
            Logger::Log(LOG_ERROR, "Failed to open small file: " + entry.full.string());
            return false;
        }
        size_t start = batch_buffer_.size();
        batch_buffer_.resize(start + entry.size);
        in.read(batch_buffer_.data() + start, entry.size);
        if (in.gcount() != static_cast<std::streamsize>(entry.size)) {
            Logger::Log(LOG_ERROR, "Failed to read small file: " + entry.full.string());
            batch_buffer_.resize(start);
            return false;
        }
        file_indices_.push_back(idx);
        file_sizes_.push_back(entry.size);
        current_size_ += entry.size;
        Logger::Log(LOG_DEBUG, "Added small file to batch: " + entry.relative);
        return true;
    }
    FileChunk make_chunk() {
        FileChunk chunk;
        chunk.type = FileChunk::SMALL_FILE_BATCH;
        chunk.data = std::move(batch_buffer_);
        chunk.batch_file_indices = std::move(file_indices_);
        chunk.batch_file_sizes = std::move(file_sizes_);
        batch_buffer_.clear(); batch_buffer_.reserve(SMALL_FILE_BATCH_SIZE);
        file_indices_.clear(); file_sizes_.clear();
        current_size_ = 0;
        return chunk;
    }
    bool has_files() const { return !file_indices_.empty(); }
};

// --- Pack folder ---
bool pack_folder(const std::string& folderPath, const std::string& packedFilePath) {
    Logger::Log(LOG_INFO, "Packing folder: " + folderPath);
    auto files = build_file_list(folderPath);
    uint32_t file_count = files.size();
    std::ofstream out(packedFilePath, std::ios::binary);
    if (!out) { Logger::Log(LOG_ERROR, "Cannot create packed file"); return false; }
    out.write(reinterpret_cast<const char*>(&file_count), sizeof(file_count));

    ChunkQueue queue;
    queue.set_capacity(QUEUE_MAX_BUFFERS);
    std::atomic<bool> reader_failed(false);

    // reader thread
    std::thread reader([&] {
        Logger::Log(LOG_DEBUG, "Reader thread started");
        try {
            SmallFileBatcher batcher;
            std::vector<char> buf(BUFFER_SIZE);
            for (uint32_t i = 0; i < file_count; i++) {
                const auto& entry = files[i];
                if (entry.is_small_file()) {
                    if (!batcher.add_file(i, entry)) {
                        queue.push(batcher.make_chunk());
                        batcher.add_file(i, entry);
                    }
                    continue;
                }
                if (batcher.has_files()) {
                    queue.push(batcher.make_chunk());
                }
                std::ifstream in(entry.full, std::ios::binary);
                if (!in) { Logger::Log(LOG_ERROR, "Failed to open " + entry.full.string()); reader_failed = true; break; }
                uint64_t left = entry.size;
                while (left > 0) {
                    size_t to_read = std::min<size_t>(left, buf.size());
                    in.read(buf.data(), to_read);
                    std::streamsize got = in.gcount();
                    if (got <= 0) break;
                    FileChunk chunk;
                    chunk.type = FileChunk::LARGE_FILE_CHUNK;
                    chunk.file_index = i;
                    chunk.data.assign(buf.begin(), buf.begin() + got);
                    queue.push(std::move(chunk));
                    left -= got;
                }
                Logger::Log(LOG_DEBUG, "Queued large file: " + entry.relative);
            }
            if (batcher.has_files()) {
                queue.push(batcher.make_chunk());
            }
        } catch (...) { reader_failed = true; }
        queue.close();
        Logger::Log(LOG_DEBUG, "Reader thread finished");
    });

    // writer
    bool ok = true;
    std::vector<bool> written(file_count, false);
    uint32_t done = 0;
    FileChunk chunk;
    while (done < file_count && queue.pop(chunk)) {
        if (chunk.type == FileChunk::SMALL_FILE_BATCH) {
            size_t off = 0;
            for (size_t j = 0; j < chunk.batch_file_indices.size(); j++) {
                uint32_t idx = chunk.batch_file_indices[j];
                const auto& entry = files[idx];
                uint32_t plen = entry.relative.size();
                out.write(reinterpret_cast<const char*>(&plen), sizeof(plen));
                out.write(entry.relative.data(), plen);
                out.write(reinterpret_cast<const char*>(&entry.size), sizeof(entry.size));
                out.write(chunk.data.data() + off, entry.size);
                off += entry.size;
                written[idx] = true;
                done++;
                Logger::Log(LOG_DEBUG, "Wrote small file: " + entry.relative);
            }
        } else {
            uint32_t idx = chunk.file_index;
            if (!written[idx]) {
                const auto& entry = files[idx];
                uint32_t plen = entry.relative.size();
                out.write(reinterpret_cast<const char*>(&plen), sizeof(plen));
                out.write(entry.relative.data(), plen);
                out.write(reinterpret_cast<const char*>(&entry.size), sizeof(entry.size));
                written[idx] = true;
                done++;
                Logger::Log(LOG_DEBUG, "Wrote header for large file: " + entry.relative);
            }
            out.write(chunk.data.data(), chunk.data.size());
        }
    }
    if (reader.joinable()) reader.join();
    if (reader_failed) ok = false;

    Logger::Log(ok ? LOG_INFO : LOG_ERROR,
                ok ? "Packing done" : "Packing failed");
    return ok;
}

// --- Unpack ---
bool unpack_packed_file(const std::string& packedFilePath, const std::string& outFolder) {
    Logger::Log(LOG_INFO, "Unpacking: " + packedFilePath);
    std::ifstream in(packedFilePath, std::ios::binary);
    if (!in) return false;
    uint32_t count;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    std::vector<char> buf(BUFFER_SIZE);
    for (uint32_t i = 0; i < count; i++) {
        uint32_t plen; in.read(reinterpret_cast<char*>(&plen), sizeof(plen));
        std::string rel(plen, '\0'); in.read(rel.data(), plen);
        uint64_t size; in.read(reinterpret_cast<char*>(&size), sizeof(size));
        fs::path outp = fs::path(outFolder) / rel;
        fs::create_directories(outp.parent_path());
        std::ofstream out(outp, std::ios::binary);
        uint64_t left = size;
        while (left > 0) {
            size_t to_read = std::min<uint64_t>(left, buf.size());
            in.read(buf.data(), to_read);
            std::streamsize got = in.gcount();
            out.write(buf.data(), got);
            left -= got;
        }
        Logger::Log(LOG_DEBUG, "Unpacked: " + rel);
    }
    Logger::Log(LOG_INFO, "Unpack complete");
    return true;
}