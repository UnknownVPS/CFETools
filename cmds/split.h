#pragma once
#include "command_base.h"
#include "../func/split_integrate/split_integrate.hpp"
#include <filesystem>
#include <stdexcept>

extern std::string save_path;

class SplitCommand : public Command {
public:
    const char* name() const override { return "split"; }
    const char* description() const override { return "Split a file with specified recoverable amount"; }
    const char* usage() const override { return "split <file> -n <total> -k <threshold>"; }
    int minArgs() const override { return 1; }
    
    int run(CommandContext& ctx) override {
        std::string file = ctx.args[0];
        
        // 1. File System Check
        if (!std::filesystem::exists(file)) {
             Logger::Log(LOG_ERROR, "File does not exist: " + file);
             return 1;
        }
        if (std::filesystem::is_directory(file)) {
            Logger::Log(LOG_ERROR, "Specified path is a folder, not a file: " + file);
            return 1;
        }

        // 2. Flag Presence Check
        if (!ctx.flags.count("n") || !ctx.flags.count("k")) {
            Logger::Log(LOG_ERROR, "Missing flags. Usage: split <file> -n <total_shares> -k <needed_to_recover>");
            return 1;
        }

        int n_val, k_val;

        // 3. Input Parsing & Logic Validation
        try {
            size_t pos_n, pos_k;
            n_val = std::stoi(ctx.flags["n"], &pos_n);
            k_val = std::stoi(ctx.flags["k"], &pos_k);

            // Check if user input garbage like "5abc"
            if (pos_n != ctx.flags["n"].length() || pos_k != ctx.flags["k"].length()) {
                throw std::invalid_argument("Trailing characters");
            }

            // Logical Bounds
            if (k_val < 1) throw std::out_of_range("k must be >= 1");
            if (n_val > 255) throw std::out_of_range("n must be <= 255");
            if (k_val > n_val) throw std::logic_error("k cannot be greater than n");

        } catch (const std::invalid_argument&) {
            Logger::Log(LOG_ERROR, "Invalid number format for -n or -k.");
            return 1;
        } catch (const std::out_of_range& e) {
            Logger::Log(LOG_ERROR, "Parameter out of range: " + std::string(e.what()));
            return 1;
        } catch (const std::logic_error& e) {
            Logger::Log(LOG_ERROR, "Logic error: " + std::string(e.what()));
            return 1;
        }
        
        Logger::Log(LOG_INFO, "Splitting file into " + std::to_string(n_val) + " shares (need " + std::to_string(k_val) + " to recover)...");
        
        // 4. Execution
        try {
            SecretSharing ss(n_val, k_val);
            // Ensure output directory exists or handle paths
            if (!ss.splitFile(file, std::filesystem::path(file).filename().stem().string())) {
                Logger::Log(LOG_ERROR, "Failed to split file (check file permissions or disk space).");
                return 1;
            }
        } catch (const std::exception& e) {
            Logger::Log(LOG_ERROR, "Internal Error: " + std::string(e.what()));
            return 1;
        }

        return 0;
    }
};

COMMAND(SplitCommand);