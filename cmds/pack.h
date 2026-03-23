#pragma once
#include "command_base.h"
#include "../func/folder_packer/folder_packer.h"
#include "../utils/compress/compress.h"
#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <atomic>

extern std::string save_path;

class PackCommand : public Command {
public:
    const char* name()        const override { return "pack"; }
    const char* description() const override { return "Pack folder into .cfup archive (use -c <level> to compress in-flight)"; }
    const char* usage()       const override { return "pack <folder> [output] [-c <level>]"; }
    int minArgs()             const override { return 1; }

    int run(CommandContext& ctx) override {
        std::string folder = ctx.args[0];

        if (!std::filesystem::is_directory(folder)) {
            Logger::Log(LOG_ERROR, "Not a folder: " + folder);
            return 1;
        }

        // Resolve compression level (0 = no compression)
        int compress_level = 0;
        if (ctx.flags.count("compress")) compress_level = std::stoi(ctx.flags.at("compress"));
        else if (ctx.flags.count("c"))   compress_level = std::stoi(ctx.flags.at("c"));

        // Resolve output filename
        std::string default_stem = std::filesystem::path(folder).filename().string();
        std::string output;
        if (ctx.args.size() >= 2) {
            output = ctx.args[1];
        } else {
            output = compress_level > 0
                ? default_stem + ".cfmp"   // packed + compressed (Redundant rn, changing later)
                : default_stem + ".cfup";
        }
        std::filesystem::path outPath = std::filesystem::path(save_path) / output;

        if (compress_level == 0) {
            // ── Plain pack, no chaining ──────────────────────────────
            Logger::Log(LOG_INFO, "Packing folder: " + folder);
            Logger::Log(LOG_INFO, "Output: " + outPath.string());
            Logger::StartTimer("Folder packing");
            bool ok = pack_folder(folder, outPath.string());
            Logger::EndTimer("Folder packing", LOG_INFO);
            if (!ok) { Logger::Log(LOG_ERROR, "Packing failed"); return 1; }
        } else {
            // ── Zero-copy: pack → compress, no temp file ─────────────
            Logger::Log(LOG_INFO, "Packing + compressing (level " +
                        std::to_string(compress_level) + "): " + folder);
            Logger::Log(LOG_INFO, "Output: " + outPath.string());
            Logger::StartTimer("Pack+Compress");

            FILE* outfile = fopen(outPath.string().c_str(), "wb");
            if (!outfile) {
                Logger::Log(LOG_ERROR, "Cannot create output file: " + outPath.string());
                return 1;
            }

            // ── Pipe: pack_folder_stream → compressStream ─────────────
            // We connect them through an in-process bounded pipe implemented
            // with a small mutex+cv queue of 4 MB byte-vector chunks.
            // pack runs on a producer thread; compressStream runs on this thread.

            constexpr size_t CHUNK = 4 * 1024 * 1024;
            constexpr size_t MAX_QUEUED = 4; // at most 4 chunks buffered

            struct Pipe {
                std::queue<std::vector<uint8_t>> q;
                std::mutex                        mtx;
                std::condition_variable           cv_push, cv_pop;
                bool                              done   = false;
                bool                              error  = false;

                // Called by producer: push a chunk (blocks if queue full)
                bool push(const void* data, size_t len) {
                    std::unique_lock<std::mutex> lk(mtx);
                    cv_push.wait(lk, [this]{ return q.size() < MAX_QUEUED || done; });
                    if (done) return false; // consumer signalled early exit
                    std::vector<uint8_t> chunk(static_cast<const uint8_t*>(data),
                                               static_cast<const uint8_t*>(data) + len);
                    q.push(std::move(chunk));
                    lk.unlock();
                    cv_pop.notify_one();
                    return true;
                }

                // Called by producer when done
                void close(bool ok) {
                    std::lock_guard<std::mutex> lk(mtx);
                    done  = true;
                    error = !ok;
                    cv_pop.notify_all();
                }

                // Called by consumer ReadFn: pull bytes into buf
                size_t read(void* buf, size_t len) {
                    // Drain current chunk first
                    uint8_t* out = static_cast<uint8_t*>(buf);
                    size_t   got = 0;

                    std::unique_lock<std::mutex> lk(mtx);
                    while (got < len) {
                        // Drain partial chunk if we have one in progress
                        if (!current.empty()) {
                            size_t take = std::min(len - got, current.size() - cur_pos);
                            memcpy(out + got, current.data() + cur_pos, take);
                            got     += take;
                            cur_pos += take;
                            if (cur_pos == current.size()) { current.clear(); cur_pos = 0; }
                            continue;
                        }
                        // Wait for a new chunk
                        cv_pop.wait(lk, [this]{ return !q.empty() || done; });
                        if (q.empty()) break; // done and no more data
                        current = std::move(q.front()); q.pop(); cur_pos = 0;
                        cv_push.notify_one();
                    }
                    return got;
                }

                std::vector<uint8_t> current;
                size_t               cur_pos = 0;
            };

            auto pipe = std::make_shared<Pipe>();

            // Producer thread: runs pack_folder_stream, pushes bytes into pipe
            std::atomic<bool> pack_ok{false};
            std::thread producer([&, pipe]() {
                WriteFn to_pipe = [&pipe](const void* buf, size_t len) -> bool {
                    return pipe->push(buf, len);
                };
                bool ok = pack_folder_stream(folder, to_pipe);
                pack_ok.store(ok);
                pipe->close(ok);
            });

            // Consumer: compressStream reads from pipe, writes to outfile
            ReadFn  from_pipe  = [&pipe](void* b, size_t n) -> size_t { return pipe->read(b, n); };
            WriteFn to_outfile = write_fn_to_file(outfile);

            bool compress_ok = compressStream(from_pipe, to_outfile, compress_level, 0);
            producer.join();
            fclose(outfile);

            Logger::EndTimer("Pack+Compress", LOG_INFO);

            bool overall_ok = pack_ok.load() && compress_ok;
            if (!overall_ok) {
                Logger::Log(LOG_ERROR, "Pack+compress failed");
                std::filesystem::remove(outPath);
                return 1;
            }
        }

        Logger::Log(LOG_INFO, "Done: " + outPath.string());
        return 0;
    }
};

COMMAND(PackCommand)