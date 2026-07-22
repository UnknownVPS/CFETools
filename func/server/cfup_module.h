#pragma once
#include "imodule.h"
#include "../folder_packer/folder_packer.h"
#include <string>
#include <vector>
#include <filesystem>

class CfupModule : public IModule {
public:
    // fs_root is needed to resolve files when intercepting /__cfup_open__ routes
    CfupModule(std::string mount_path, std::string fs_root, size_t page_size);
    bool init(ServerCore&) override;
    bool handle(ModuleContext& ctx) override;

private:
    std::string mount_path_;
    std::string fs_root_;
    size_t     page_size_;

    // TOC for the primary mounted archive (if started directly on a .cfup file)
    std::vector<PackedFileEntry>     entries_;
    std::string                      archive_path_;
    std::filesystem::file_time_type  mtime_{};
    std::string                      mtime_str_;
};