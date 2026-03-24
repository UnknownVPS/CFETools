#pragma once
#include "command_base.h"
#include "../func/patch_creation/patch.h"
#include <filesystem>

extern std::string save_path;

class DiffCommand : public Command {
public:
    const char* name() const override { 
        return "diff"; 
    }
    
    const char* description() const override { 
        return "Create binary diff patch or file signature"; 
    }
    
    const char* usage() const override { 
        return "diff <src> <dst> <patch>           (Standard)\n"
               "       diff <src> <sig_file> --sig        (Create Signature)\n"
               "       diff <sig> <dst> <patch> --sig      (Patch from Signature)"; 
    }
    
    int minArgs() const override { 
        return 2; 
    }
    
    int run(CommandContext& ctx) override {
        // Check for signature mode toggle
        bool isSigMode = ctx.boolFlags.count("sig") || ctx.boolFlags.count("s");

        // --- MODE 1: Create Signature (-s <src> <sig_file>) ---
        if (isSigMode && ctx.args.size() == 2) {
            std::string src = ctx.args[0];
            std::string sig = ctx.args[1];
            std::filesystem::path sigPath = std::filesystem::path(save_path) / sig;

            if (!std::filesystem::exists(src)) {
                Logger::Log(LOG_ERROR, "Source file missing: " + src);
                return 1;
            }

            Logger::Log(LOG_INFO, "Generating 128-bit remote signature...");
            Logger::StartTimer("Signature generation");
            fastcdc::createSignature(src.c_str(), sigPath.string().c_str());
            Logger::EndTimer("Signature generation", LOG_INFO);
            return 0;
        }

        // --- MODE 2: Patch using Signature (-s <sig> <dst> <patch>) ---
        if (isSigMode && ctx.args.size() == 3) {
            std::string sig   = ctx.args[0];
            std::string dst   = ctx.args[1];
            std::string patch = ctx.args[2];
            std::filesystem::path patchPath = std::filesystem::path(save_path) / patch;

            if (!std::filesystem::exists(sig)) {
                Logger::Log(LOG_ERROR, "Signature file missing: " + sig);
                return 1;
            }
            if (!std::filesystem::exists(dst)) {
                Logger::Log(LOG_ERROR, "Destination file missing: " + dst);
                return 1;
            }

            Logger::Log(LOG_INFO, "Creating diff from signature (No local source)...");
            Logger::StartTimer("Sig-Patch generation");
            fastcdc::createPatchFromSig(sig.c_str(), dst.c_str(), patchPath.string().c_str());
            Logger::EndTimer("Sig-Patch generation", LOG_INFO);
            return 0;
        }

        // --- MODE 3: Standard Diff (3 args, no flags) ---
        if (!isSigMode && ctx.args.size() == 3) {
            std::string src   = ctx.args[0];
            std::string dst   = ctx.args[1];
            std::string patch = ctx.args[2];
            std::filesystem::path patchPath = std::filesystem::path(save_path) / patch;

            if (!std::filesystem::exists(src) || !std::filesystem::exists(dst)) {
                Logger::Log(LOG_ERROR, "Source or Destination file missing.");
                return 1;
            }

            Logger::Log(LOG_INFO, "Creating standard binary diff...");
            Logger::StartTimer("Diff generation");
            fastcdc::createPatch(src.c_str(), dst.c_str(), patchPath.string().c_str());
            Logger::EndTimer("Diff generation", LOG_INFO);
            return 0;
        }

        Logger::Log(LOG_ERROR, "Invalid argument count for selected mode.");
        std::cout << usage() << std::endl;
        return 1;
    }
};

COMMAND(DiffCommand)