#pragma once
#include "command_base.h"
#include "../func/split_integrate/split_integrate.hpp"
#include <filesystem>
#include <vector>
#include <string>
#include <fstream>
#include <iomanip>

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
    
    int minArgs() const override { 
        return 2; 
    }
    
    int run(CommandContext& ctx) override {
        std::string output_file = ctx.args[0];
        
        // Build full output path in save_path (like CompressCommand does)
        std::filesystem::path full_output_path = std::filesystem::path(ctx.config.save_path) / output_file;
        
        std::vector<std::string> share_files;
        for(size_t i = 1; i < ctx.args.size(); i++) {
            share_files.push_back(ctx.args[i]);
        }

        // Validate share files exist
        for (const auto& path : share_files) {
            if (!std::filesystem::exists(path)) {
                Logger::Log(LOG_ERROR, "Share file not found: " + path);
                return 1;
            }
        }

        // Read metadata from first share to determine k
        uint32_t k_required = 0;
        uint64_t original_size = 0;
        try {
            std::ifstream probe(share_files[0], std::ios::binary);
            if (!probe) {
                Logger::Log(LOG_ERROR, "Cannot open first share: " + share_files[0]);
                return 1;
            }
            
            probe.seekg(1);  // Skip share ID
            probe.read(reinterpret_cast<char*>(&k_required), sizeof(uint32_t));
            probe.read(reinterpret_cast<char*>(&original_size), sizeof(uint64_t));
            
            if (!probe || k_required < 1 || k_required > 255) {
                Logger::Log(LOG_ERROR, "Invalid or corrupted share header");
                return 1;
            }
        } catch (const std::exception& e) {
            Logger::Log(LOG_ERROR, "Error reading share header: " + std::string(e.what()));
            return 1;
        }

        // Display share information
        Logger::Log(LOG_INFO, "Share Information:");
        Logger::Log(LOG_INFO, "  Threshold (k): " + std::to_string(k_required));
        Logger::Log(LOG_INFO, "  Shares provided: " + std::to_string(share_files.size()));
        Logger::Log(LOG_INFO, "  Original file size: " + std::to_string(original_size) + " bytes");

        if (share_files.size() < k_required) {
            Logger::Log(LOG_ERROR, "Insufficient shares!");
            Logger::Log(LOG_ERROR, "  Required: " + std::to_string(k_required));
            Logger::Log(LOG_ERROR, "  Provided: " + std::to_string(share_files.size()));
            return 1;
        }

        // Reconstruct
        try {
            SecretSharing ss(k_required, k_required);
            
            // Display reconstruction info using helper methods
            Logger::Log(LOG_INFO, "Reconstruction Parameters:");
            Logger::Log(LOG_INFO, "  Using threshold: " + std::to_string(ss.getThreshold()) + " shares");
            Logger::Log(LOG_INFO, "  Output location: " + full_output_path.string());
            
            if (share_files.size() > (size_t)ss.getThreshold()) {
                Logger::Log(LOG_INFO, "  Excess shares: " + 
                           std::to_string(share_files.size() - ss.getThreshold()) + 
                           " (will use first " + std::to_string(ss.getThreshold()) + ")");
            }
            
            Logger::Log(LOG_INFO, "Starting reconstruction...");
            
            if (!ss.integrateShares(share_files, full_output_path.string())) {
                Logger::Log(LOG_ERROR, "Reconstruction failed");
                Logger::Log(LOG_INFO, "Possible causes:");
                Logger::Log(LOG_INFO, "  - Shares are from different split operations");
                Logger::Log(LOG_INFO, "  - Share files are corrupted");
                Logger::Log(LOG_INFO, "  - HMAC integrity check failed (tampering detected)");
                Logger::Log(LOG_INFO, "  - Incompatible share versions");
                return 1;
            }
            
            Logger::Log(LOG_INFO, "File reconstructed successfully!");
            
            // Verify and show output info
            if (std::filesystem::exists(full_output_path)) {
                auto size = std::filesystem::file_size(full_output_path);
                Logger::Log(LOG_INFO, "Output: " + full_output_path.string());
                Logger::Log(LOG_INFO, "  Size: " + std::to_string(size) + " bytes");
                
                if (size == original_size) {
                    Logger::Log(LOG_INFO, "✓ Size matches original");
                } else {
                    Logger::Log(LOG_WARNING, "Size mismatch (expected: " + 
                               std::to_string(original_size) + " bytes)");
                }
            }

        } catch (const std::invalid_argument& e) {
            Logger::Log(LOG_ERROR, "Invalid parameters: " + std::string(e.what()));
            return 1;
        } catch (const std::runtime_error& e) {
            Logger::Log(LOG_ERROR, "Runtime error: " + std::string(e.what()));
            return 1;
        } catch (const std::exception& e) {
            Logger::Log(LOG_ERROR, "Unexpected error: " + std::string(e.what()));
            return 1;
        }

        return 0;
    }
};

COMMAND(IntegrateCommand);