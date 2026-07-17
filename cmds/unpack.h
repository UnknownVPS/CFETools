#pragma once
#include "command_base.h"
#include "../func/folder_packer/folder_packer.h"
#include "../utils/compress/decompress.h"
#include <filesystem>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <atomic>
#include <cstring>


class UnpackCommand : public Command {
public:
    const char* name()        const override { return "unpack"; }
    const char* description() const override { return "Unpack .cfup archive (auto-decompresses if needed)"; }
    const char* usage()       const override { return "unpack <archive.cfup[.cfmp]> [output_dir]"; }
    int minArgs()             const override { return 1; }

    int run(CommandContext& ctx) override {
        std::string archive = ctx.args[0];

        if (!std::filesystem::exists(archive)) {
            Logger::Log(LOG_ERROR, "Archive not found: " + archive);
            return 1;
        }

        std::string out_stem = std::filesystem::path(archive).stem().string();
        if (std::filesystem::path(out_stem).extension() == ".cfup")
            out_stem = std::filesystem::path(out_stem).stem().string();

        std::string output_dir = ctx.args.size() >= 2 ? ctx.args[1] : out_stem;
        std::filesystem::path outPath = std::filesystem::path(ctx.config.save_path) / output_dir;

        std::ifstream probe(archive, std::ios::binary);
        if (!probe) { Logger::Log(LOG_ERROR, "Cannot open: " + archive); return 1; }
        uint8_t peek[8] = {};
        probe.read(reinterpret_cast<char*>(peek), 8);
        probe.close();

        CompressionFormat fmt = detectFormatFromHeader(peek);
        bool is_compressed    = (fmt != CompressionFormat::UNKNOWN);
        bool direct_cfup      = looks_like_cfup_header(peek, 8);

        if (!is_compressed && !direct_cfup) {
            Logger::Log(LOG_ERROR, "File is neither a recognised compressed format nor a .cfup archive");
            return 1;
        }

        if (direct_cfup && !is_compressed) {
            Logger::Log(LOG_INFO, "Unpacking: " + archive);
            Logger::Log(LOG_INFO, "Output: " + outPath.string());
            Logger::StartTimer("Folder unpacking");
            bool ok = unpack_packed_file(archive, outPath.string());
            Logger::EndTimer("Folder unpacking", LOG_INFO);
            if (!ok) { Logger::Log(LOG_ERROR, "Unpack failed"); return 1; }
        } else {
            Logger::Log(LOG_INFO, "Decompressing + unpacking: " + archive);
            Logger::Log(LOG_INFO, "Output: " + outPath.string());
            Logger::StartTimer("Decompress+Unpack");

            constexpr size_t MAX_QUEUE = 4;

            struct Pipe {
                std::queue<std::vector<uint8_t>> q;
                std::mutex                        mtx;
                std::condition_variable           cv_push, cv_pop;
                bool                              done  = false;
                bool                              error = false;

                bool push(const void* data, size_t len) {
                    std::unique_lock<std::mutex> lk(mtx);
                    cv_push.wait(lk, [this]{ return q.size() < MAX_QUEUE || done; });
                    if (done) return false;
                    std::vector<uint8_t> chunk(static_cast<const uint8_t*>(data),
                                               static_cast<const uint8_t*>(data) + len);
                    q.push(std::move(chunk));
                    lk.unlock();
                    cv_pop.notify_one();
                    return true;
                }
                void close(bool ok) {
                    std::lock_guard<std::mutex> lk(mtx);
                    done = true; error = !ok;
                    cv_pop.notify_all();
                }
                size_t read(void* buf, size_t len) {
                    uint8_t* out = static_cast<uint8_t*>(buf);
                    size_t   got = 0;
                    std::unique_lock<std::mutex> lk(mtx);
                    while (got < len) {
                        if (!current.empty()) {
                            size_t take = std::min(len - got, current.size() - cur_pos);
                            memcpy(out + got, current.data() + cur_pos, take);
                            got += take; cur_pos += take;
                            if (cur_pos == current.size()) { current.clear(); cur_pos = 0; }
                            continue;
                        }
                        cv_pop.wait(lk, [this]{ return !q.empty() || done; });
                        if (q.empty()) break;
                        current = std::move(q.front()); q.pop(); cur_pos = 0;
                        cv_push.notify_one();
                    }
                    return got;
                }
                std::vector<uint8_t> current;
                size_t               cur_pos = 0;
            };

            auto pipe = std::make_shared<Pipe>();

            std::atomic<bool> decomp_ok{false};
            std::thread producer([&, pipe]() {
                FILE* infile = fopen(archive.c_str(), "rb");
                if (!infile) { pipe->close(false); return; }

                // Seek past the 8 bytes already peeked — decompressStream
                // replays them internally via leftover[], so fread must start
                // at offset 8 or those bytes are fed to the decompressor twice.
                fseek(infile, 8, SEEK_SET);

                uint8_t hdr[8];
                memcpy(hdr, peek, 8);

                ReadFn from_file = [infile](void* b, size_t n) -> size_t {
                    return fread(b, 1, n, infile);
                };
                WriteFn to_pipe = [&pipe](const void* b, size_t n) -> bool {
                    return pipe->push(b, n);
                };

                bool ok = decompressStream(hdr, from_file, to_pipe);
                fclose(infile);
                decomp_ok.store(ok);
                pipe->close(ok);
            });

            ReadFn from_pipe = [&pipe](void* b, size_t n) -> size_t {
                return pipe->read(b, n);
            };

            bool unpack_ok = unpack_stream(from_pipe, outPath.string());
            producer.join();

            Logger::EndTimer("Decompress+Unpack", LOG_INFO);

            if (!decomp_ok.load() || !unpack_ok) {
                Logger::Log(LOG_ERROR, "Decompress+unpack failed");
                return 1;
            }
        }

        Logger::Log(LOG_INFO, "Extracted to: " + outPath.string());
        return 0;
    }
};

COMMAND(UnpackCommand)