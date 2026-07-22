#pragma once
#include "imodule.h"
#include <string>

class DirModule : public IModule {
public:
    DirModule(std::string root, bool follow_symlinks, size_t page_size);
    bool handle(ModuleContext& ctx) override;

private:
    std::string root_;
    bool       follow_symlinks_;
    size_t     page_size_;
};