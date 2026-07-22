#pragma once
#include "command_base.h"
#include "../func/folder_packer/folder_packer.h"
#include <filesystem>

class MigrateCommand : public Command {
public:
    const char* name()        const override { return "migrate"; }
    const char* description() const override { return "Migrate a legacy CFUP v1 archive to v2 format (adds TOC, hashes, path safety)"; }
    const char* usage()       const override { return "migrate <input_v1.cfup> [output_v2.cfup]"; }
    int minArgs()             const override { return 1; }

    int run(CommandContext& ctx) override {
        std::string input_path = ctx.args[0];

        if (!std::filesystem::exists(input_path) || !std::filesystem::is_regular_file(input_path)) {
            Logger::Log(LOG_ERROR, "Input file not found: " + input_path);
            return 1;
        }

        // Safety check: Don't accidentally corrupt a v2 file by running it through the v1 parser
        if (looks_like_cfup(input_path)) {
            Logger::Log(LOG_ERROR, "File appears to already be CFUP v2 (magic header found). Migration is for v1 archives only.");
            return 1;
        }

        // Resolve output filename
        std::filesystem::path inPath(input_path);
        std::string output;
        
        if (ctx.args.size() >= 2) {
            output = ctx.args[1];
            // Ensure it has the correct extension if they forgot it
            if (output.size() < 5 || output.compare(output.size() - 5, 5, ".cfup") != 0) {
                output += ".cfup";
            }
        } else {
            // Default to the original filename, saved in the configured save directory
            output = inPath.filename().string();
        }

        std::filesystem::path outPath = std::filesystem::path(ctx.config.save_path) / output;

        Logger::Log(LOG_INFO, "Migrating v1 -> v2: " + input_path);
        Logger::Log(LOG_INFO, "Output: " + outPath.string());
        Logger::StartTimer("Migration");

        bool ok = migrate_cfup_v1_to_v2(input_path, outPath.string());

        Logger::EndTimer("Migration", LOG_INFO);

        if (!ok) {
            Logger::Log(LOG_ERROR, "Migration failed (check if input is a valid v1 CFUP)");
            return 1;
        }

        Logger::Log(LOG_INFO, "Done: " + outPath.string());
        return 0;
    }
};

COMMAND(MigrateCommand)