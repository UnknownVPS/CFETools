#pragma once
#include "command_base.h"
#include "../func/img_creation/create_img.h"
#include "../func/folder_packer/folder_packer.h"
#include "../utils/compress/compress.h"
#include <filesystem>

extern std::string save_path;
extern bool isPacked;
extern bool isCompressed;

class EncodeCommand : public Command {
public:
    const char* name() const override { 
        return "encode"; 
    }
    
    const char* description() const override { 
        return "Encode file or folder to image"; 
    }
    
    const char* usage() const override { 
        return "encode <file/folder> [--compress <level>]"; 
    }
    
    int minArgs() const override { 
        return 1; 
    }
    
    int run(CommandContext& ctx) override {
        std::string file_input = ctx.args[0];
        
        if (!std::filesystem::exists(file_input)) {
            Logger::Log(LOG_ERROR, "Input path does not exist: " + file_input);
            return 1;
        }
        
        Logger::Log(LOG_INFO, "Encoding file/folder: " + file_input);
        std::filesystem::path inputPath(file_input);
        std::string path_to_encode;
        
        // Handle folder: pack it first
        if (std::filesystem::is_directory(inputPath)) {
            std::string packedFileName = inputPath.filename().string() + ".cfup";
            std::filesystem::path packedFilePath = std::filesystem::path(save_path) / packedFileName;
            
            Logger::Log(LOG_INFO, "Packing folder to: " + packedFilePath.string());
            Logger::StartTimer("Folder packing");
            
            if (!pack_folder(file_input, packedFilePath.string())) {
                Logger::Log(LOG_ERROR, "Failed to pack folder");
                return 1;
            }
            
            Logger::EndTimer("Folder packing", LOG_INFO);
            isPacked = true;
            path_to_encode = packedFilePath.string();
        } else {
            path_to_encode = file_input;
        }
        
        // Handle compression
        if (ctx.flags.count("compress") || ctx.flags.count("c")) {
            std::string compress_arg = ctx.flags.count("compress") ? ctx.flags["compress"] : ctx.flags["c"];
            
            std::filesystem::path encodePath(path_to_encode);
            std::string compressedFilename = encodePath.filename().string() + ".cfmp";
            std::filesystem::path compressedFilePath = std::filesystem::path(save_path) / compressedFilename;
            int compress_level = std::stoi(compress_arg);
            
            Logger::StartTimer("File compression");
            
            if (!compressFile(path_to_encode, compressedFilePath.string(), compress_level)) {
                Logger::Log(LOG_ERROR, "Failed to compress file");
                return 1;
            }
            
            Logger::EndTimer("File compression", LOG_INFO);
            
            // Clean up intermediate packed file
            if (isPacked) {
                std::filesystem::remove(path_to_encode);
            }
            
            isCompressed = true;
            path_to_encode = compressedFilePath.string();
        }
        
        // Encode to image
        Logger::StartTimer("Encoding to image");
        create_img(path_to_encode);
        Logger::EndTimer("Encoding to image", LOG_INFO);
        
        // Clean up temporary files
        if (isPacked || isCompressed) {
            std::filesystem::remove(path_to_encode);
            Logger::Log(LOG_DEBUG, "Cleaned up temporary file");
        }
        
        return 0;
    }
};

COMMAND(EncodeCommand)
