#pragma once
#include "command_base.h"
#include "../utils/hashers/fileHasher.hpp"
#include <filesystem>

extern bool shaEnabled;
extern bool crcEnabled;

class HashCommand : public Command {
public:
    const char* name() const override { 
        return "hash"; 
    }
    
    const char* description() const override { 
        return "Calculate file hashes"; 
    }
    
    const char* usage() const override { 
        return "hash <file> [--sha] [--crc]"; 
    }
    
    int minArgs() const override { 
        return 1; 
    }
    
    int run(CommandContext& ctx) override {
        std::string file = ctx.args[0];
        
        if (!std::filesystem::exists(file)) {
            Logger::Log(LOG_ERROR, "File does not exist: " + file);
            return 1;
        }
        
        bool anyHash = false;
        
        // Check global flags set by main
        if (shaEnabled) {
            std::string hash = fileHasher::hashFileSHA256(file);
            Logger::Log(LOG_INFO, "SHA-256: " + hash);
            anyHash = true;
        }
        if (crcEnabled) {
            std::string crc = fileHasher::crc32_file(file);
            Logger::Log(LOG_INFO, "CRC32: " + crc);
            anyHash = true;
        }
        
        // Default to xxHash if no specific hash requested
        if (!anyHash) {
            std::string hash = fileHasher::xxhash_file(file);
            Logger::Log(LOG_INFO, "xxHash64: " + hash);
        }
        
        return 0;
    }
};

COMMAND(HashCommand)