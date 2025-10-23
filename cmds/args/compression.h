#pragma once
#include "arg_base.h"

class CompressionArgs : public ArgGroup {
public:
    const char* group() const override { 
        return "Compression"; 
    }
    
    std::vector<ArgDef> definitions() const override {
        return {
            ArgDef("compress", "c", "Compression level (1-21)", false, "0"),
        };
    }
};

ARG_GROUP(CompressionArgs)
