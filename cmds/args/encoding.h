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
        };
    }
};

ARG_GROUP(EncodingArgs)
