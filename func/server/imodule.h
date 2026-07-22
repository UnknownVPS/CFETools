#pragma once
#include <string>
#include <unordered_map>
namespace httplib { struct Request; struct Response; }
class ServerCore;

struct ModuleContext {
    std::string url_path;
    std::string decoded_path;
    std::unordered_map<std::string, std::string> query;
    const httplib::Request& req;
    httplib::Response& res;
    ServerCore& core;
};

class IModule {
public:
    virtual ~IModule() = default;
    virtual bool init(ServerCore&) { return true; }
    virtual bool handle(ModuleContext& ctx) = 0;
};