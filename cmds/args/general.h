#pragma once
#include "arg_base.h"

class GeneralArgs : public ArgGroup {
public:
    const char* group() const override { 
        return "General"; 
    }
    
    std::vector<ArgDef> definitions() const override {
        return {
            ArgDef("save-path", "sp", "Use a custom save path", false, ""),
        };
    }
};

ARG_GROUP(GeneralArgs)
