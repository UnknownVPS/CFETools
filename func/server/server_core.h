#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include <vector>
#include <filesystem>

namespace httplib { struct Request; struct Response; class Server; }
class IModule;

struct ServerConfig {
    std::string root_path;
    int port             = 8080;
    size_t page_size     = 100;
    int thread_pool_size = 1;
    bool follow_symlinks = false;
};

class ServerCore {
public:
    explicit ServerCore(ServerConfig cfg);
    ~ServerCore();
    void register_module(std::shared_ptr<IModule> mod);
    int run();
    const ServerConfig& config() const { return cfg_; }
    httplib::Server& server() { return *svr_; }

    // Shared helpers
    static std::string url_decode(const std::string& s);
    static std::string html_escape(const std::string& s);
    static std::string format_size(uintmax_t bytes);
    static std::string time_to_string(std::filesystem::file_time_type t);
    static std::string get_timestamp();

private:
    ServerConfig cfg_;
    std::unique_ptr<httplib::Server> svr_;
    std::vector<std::shared_ptr<IModule>> modules_;
    void handle_request_(const httplib::Request& req, httplib::Response& res);
};