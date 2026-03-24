#pragma once
#include "command_base.h"
#include "../func/patch_creation/patch.h"
#include <filesystem>
#include <fstream>

extern std::string save_path;

class DiffCommand : public Command {
private:
    // Helper to detect if a signature file is 64-bit or 128-bit
    // Returns 128 for "SIGN", 64 for "SIG6", or 0 if invalid/error
    int detectSignatureBits(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) return 0;
        uint32_t magic = 0;
        file.read(reinterpret_cast<char*>(&magic), 4);
        if (magic == 0x5349474E) return 128; // "SIGN"
        if (magic == 0x53494736) return 64;  // "SIG6"
        return 0;
    }

public:
    const char* name() const override { 
        return "diff"; 
    }
    
    const char* description() const override { 
        return "Create binary diff patch or file signature"; 
    }
    
    const char* usage() const override { 
        return "diff <src> <dst> <patch>           (Standard Diff)\n"
               "       diff <src> <sig_file> --sig        (Create Signature)\n"
               "       diff <sig> <dst> <patch> --sig      (Patch from Signature)\n"
               "Options:\n"
               "       --hash128  Use 128-bit hashes (64-bit is default for NEW files)\n"
               "       Note: Bit-depth is auto-detected when reading existing signatures."; 
    }
    
    int minArgs() const override { 
        return 2; 
    }
    
    int run(CommandContext& ctx) override {
        bool isSigMode = ctx.boolFlags.count("sig") || ctx.boolFlags.count("s");
        
        // Only used for CREATION modes. Detection is used for READING modes.
        bool force128 = ctx.boolFlags.count("hash128");

        // --- MODE 1: Create Signature ---
        if (isSigMode && ctx.args.size() == 2) {
            std::string src = ctx.args[0];
            std::string sig = ctx.args[1];
            std::filesystem::path sigPath = std::filesystem::path(save_path) / sig;

            if (!std::filesystem::exists(src)) {
                Logger::Log(LOG_ERROR, "Source file missing: " + src);
                return 1;
            }

            Logger::Log(LOG_INFO, "Generating " + std::string(force128 ? "128-bit" : "64-bit") + " signature...");
            Logger::StartTimer("Signature generation");
            if (force128) {
                fastcdc::createSignature(src.c_str(), sigPath.string().c_str());
            } else {
                fastcdc::createSignature64(src.c_str(), sigPath.string().c_str());
            }
            Logger::EndTimer("Signature generation", LOG_INFO);
            return 0;
        }

        // --- MODE 2: Patch using Signature (Auto-Detects Bit-Depth) ---
        if (isSigMode && ctx.args.size() == 3) {
            std::string sig   = ctx.args[0];
            std::string dst   = ctx.args[1];
            std::string patch = ctx.args[2];
            std::filesystem::path patchPath = std::filesystem::path(save_path) / patch;

            if (!std::filesystem::exists(sig) || !std::filesystem::exists(dst)) {
                Logger::Log(LOG_ERROR, "Required files missing for signature patching.");
                return 1;
            }

            int bits = detectSignatureBits(sig);
            if (bits == 0) {
                Logger::Log(LOG_ERROR, "Invalid or corrupt signature file: " + sig);
                return 1;
            }

            Logger::Log(LOG_INFO, "Detected " + std::to_string(bits) + "-bit signature. Creating patch...");
            Logger::StartTimer("Sig-Patch generation");
            if (bits == 128) {
                fastcdc::createPatchFromSig(sig.c_str(), dst.c_str(), patchPath.string().c_str());
            } else {
                fastcdc::createPatchFromSig64(sig.c_str(), dst.c_str(), patchPath.string().c_str());
            }
            Logger::EndTimer("Sig-Patch generation", LOG_INFO);
            return 0;
        }

        // --- MODE 3: Standard Diff ---
        if (!isSigMode && ctx.args.size() == 3) {
            std::string src   = ctx.args[0];
            std::string dst   = ctx.args[1];
            std::string patch = ctx.args[2];
            std::filesystem::path patchPath = std::filesystem::path(save_path) / patch;

            if (!std::filesystem::exists(src) || !std::filesystem::exists(dst)) {
                Logger::Log(LOG_ERROR, "Source or Destination file missing.");
                return 1;
            }

            Logger::Log(LOG_INFO, "Creating " + std::string(force128 ? "128-bit" : "64-bit") + " binary diff...");
            Logger::StartTimer("Diff generation");
            if (force128) {
                fastcdc::createPatch(src.c_str(), dst.c_str(), patchPath.string().c_str());
            } else {
                fastcdc::createPatch64(src.c_str(), dst.c_str(), patchPath.string().c_str());
            }
            Logger::EndTimer("Diff generation", LOG_INFO);
            return 0;
        }

        Logger::Log(LOG_ERROR, "Invalid argument count.");
        std::cout << usage() << std::endl;
        return 1;
    }
};

COMMAND(DiffCommand)