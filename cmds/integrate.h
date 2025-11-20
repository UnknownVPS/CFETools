#pragma once
#include "command_base.h"
#include "../func/split_integrate/split_integrate.hpp"
#include <filesystem>
#include <vector>
#include <string>
#include <fstream>

extern std::string save_path;

class IntegrateCommand : public Command {
public:
    const char* name() const override { 
        return "integrate"; 
    }
    
    const char* description() const override { 
        return "Reconstruct a file from split shares"; 
    }
    
    const char* usage() const override { 
        return "integrate <output_file> <share_file_1> <share_file_2> ..."; 
    }
    
    // Need at least output file + 1 share to even attempt inspection
    int minArgs() const override { 
        return 2; 
    }
    
    int run(CommandContext& ctx) override {
        // 1. Parse Arguments
        // Syntax: integrate <output> <share1> <share2> ...
        std::string output_file = ctx.args[0];
        
        std::vector<std::string> share_files;
        for(size_t i = 1; i < ctx.args.size(); i++) {
            share_files.push_back(ctx.args[i]);
        }

        // 2. Validate Input Files
        for (const auto& path : share_files) {
            if (!std::filesystem::exists(path)) {
                Logger::Log(LOG_ERROR, "Share file not found: " + path);
                return 1;
            }
        }

        // 3. Read Metadata from the FIRST share to determine 'k'
        // Header Format: [ID (1b)] [K (4b)] [Size (8b)] ...
        uint32_t k_required = 0;
        try {
            std::ifstream probe(share_files[0], std::ios::binary);
            if (!probe) {
                Logger::Log(LOG_ERROR, "Cannot open first share for inspection.");
                return 1;
            }
            
            // Skip ID (1 byte)
            probe.seekg(1, std::ios::cur);
            
            // Read K (4 bytes)
            probe.read(reinterpret_cast<char*>(&k_required), sizeof(uint32_t));
            
            if (!probe) {
                Logger::Log(LOG_ERROR, "Failed to read header metadata.");
                return 1;
            }
        } catch (const std::exception& e) {
            Logger::Log(LOG_ERROR, "Error inspecting file header: " + std::string(e.what()));
            return 1;
        }

        Logger::Log(LOG_INFO, "Detected threshold k=" + std::to_string(k_required));

        // 4. Logic Validation
        if (k_required < 1 || k_required > 255) {
            Logger::Log(LOG_ERROR, "Invalid threshold value detected in file header: " + std::to_string(k_required));
            return 1;
        }

        if (share_files.size() < k_required) {
            Logger::Log(LOG_ERROR, "Not enough shares provided. Needed: " + std::to_string(k_required) + ", Provided: " + std::to_string(share_files.size()));
            return 1;
        }

        // 5. Execution
        try {
            // Initialize SecretSharing. 
            // We pass n=255 as a safe maximum because 'n' is not strictly used 
            // in the reconstruction logic (only k is used to form the matrix), 
            // but the constructor enforces n >= k.
            SecretSharing ss(255, k_required);
            
            Logger::Log(LOG_INFO, "Reconstructing to: " + output_file);
            
            if (!ss.integrateShares(share_files, output_file)) {
                Logger::Log(LOG_ERROR, "Reconstruction failed (Check key shares or file integrity).");
                return 1;
            }
            
            Logger::Log(LOG_INFO, "Success!");

        } catch (const std::exception& e) {
            Logger::Log(LOG_ERROR, "Internal Error: " + std::string(e.what()));
            return 1;
        }

        return 0;
    }
};

COMMAND(IntegrateCommand);