#pragma once
#include "command_base.h"
#include "../utils/compress/compress.h"
#include <filesystem>

class CompressCommand : public Command {
public:
    const char* name()        const override { return "compress"; }
    const char* description() const override { return "Compress a file into a .cfmp archive"; }
    const char* usage()       const override { return "compress <file> <compression_lvl> [output.cfmp]"; }
    int minArgs()             const override { return 2; }

    int run(CommandContext& ctx) override {
        const std::string& save_path = ctx.config.save_path;
        std::string file             = ctx.args[0];
        std::string compressionLvl   = ctx.args[1];

        if (std::filesystem::is_directory(file)) {
            Logger::Log(LOG_ERROR, "Specified path is a folder: " + file);
            return 1;
        }

        std::string output = ctx.args.size() >= 3
            ? ctx.args[2]
            : std::filesystem::path(file).filename().stem().string() + ".cfmp";

        std::filesystem::path packedFilePath = std::filesystem::path(ctx.config.save_path) / output;

        Logger::Log(LOG_INFO, "Compressing file: " + file);
        Logger::Log(LOG_INFO, "Output archive: " + packedFilePath.string());
        Logger::StartTimer("Compressing");

        if (!compressFile(file, packedFilePath.string(), std::stoi(compressionLvl))) {
            Logger::Log(LOG_ERROR, "Failed to compress file");
            return 1;
        }

        Logger::EndTimer("Compressing", LOG_INFO);
        Logger::Log(LOG_INFO, "Archive created successfully");
        return 0;
    }
};

COMMAND(CompressCommand)