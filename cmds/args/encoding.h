#pragma once
#include "arg_base.h"

class EncodingArgs : public ArgGroup {
public:
    const char* group() const override { 
        return "Encoding"; 
    }
    
    std::vector<ArgDef> definitions() const override {
        return {
            ArgDef("no-encrypt", "ne", "Disable encryption", true),
            ArgDef("two-file", "2f", "Use two-file system", true),
            ArgDef("grayscale", "gs", "Use 8-bit grayscale mode", true),
            ArgDef("skip-hash", "nh", "Skip hash verification", true),
            ArgDef("compress", "c", "Compression level (1-21)", false, "0"),
        };
    }
};

ARG_GROUP(EncodingArgs)
