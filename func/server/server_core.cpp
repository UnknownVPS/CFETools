#include "server_core.h"
#include "imodule.h"
#include "httplib.h"
#include <iostream>
#include <csignal>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace fs = std::filesystem;
namespace {
    std::atomic<httplib::Server*> g_server_ptr{nullptr};
    void signal_handler(int signum) {
        std::cout << "\nReceived signal " << signum << ", shutting down gracefully..." << std::endl;
        if (auto* s = g_server_ptr.load()) s->stop();
    }
}

std::string ServerCore::url_decode(const std::string& str) {
    std::string result; result.reserve(str.size());
    for (size_t i = 0; i < str.size(); ++i) {
        if (str[i] == '%' && i + 2 < str.size()) {
            try { int v = std::stoi(str.substr(i + 1, 2), nullptr, 16); result += static_cast<char>(v); i += 2; }
            catch (...) { result += str[i]; }
        } else if (str[i] == '+') { result += ' '; } 
        else { result += str[i]; }
    }
    return result;
}

std::string ServerCore::html_escape(const std::string& s) {
    std::string out; out.reserve(s.size() * 1.2);
    for (char c : s) {
        switch (c) {
            case '&': out += "&amp;"; break; case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break; case '"': out += "&quot;"; break;
            case '\'': out += "&#39;"; break; default: out += c;
        }
    }
    return out;
}

std::string ServerCore::format_size(uintmax_t bytes) {
    if (bytes == 0) return "0 B";
    const char* sfx[] = {"B","KB","MB","GB","TB"};
    int s = 0; double v = (double)bytes;
    while (v >= 1024 && s < 4) { ++s; v /= 1024; }
    std::ostringstream ss; ss << std::fixed << std::setprecision(2) << v << " " << sfx[s];
    return ss.str();
}

std::string ServerCore::time_to_string(fs::file_time_type ftime) {
    try {
        auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
        auto t = std::chrono::system_clock::to_time_t(sctp);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream oss; oss << std::put_time(&tm, "%Y-%m-%d %H:%M");
        return oss.str();
    } catch (...) { return "-"; }
}

std::string ServerCore::get_timestamp() {
    auto now  = std::chrono::system_clock::now();
    auto t    = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream ss; ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

ServerCore::ServerCore(ServerConfig cfg) : cfg_(std::move(cfg)) { svr_ = std::make_unique<httplib::Server>(); }
ServerCore::~ServerCore() { if (svr_) svr_->stop(); }

void ServerCore::register_module(std::shared_ptr<IModule> mod) {
    if (mod && mod->init(*this)) modules_.push_back(std::move(mod));
}

void ServerCore::handle_request_(const httplib::Request& req, httplib::Response& res) {
    res.set_header("Access-Control-Allow-Origin", "*");
    res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
    res.set_header("Access-Control-Allow-Headers", "Content-Type");

    std::unordered_map<std::string, std::string> q;
    for (const auto& p : req.params) q[p.first] = p.second;
    ModuleContext ctx{ req.path, url_decode(req.path), std::move(q), req, res, *this };

    for (auto& m : modules_) {
        if (m->handle(ctx)) return;   // First module to claim wins
    }
    res.status = 404; res.set_content("Not Found", "text/plain");
}

int ServerCore::run() {
    g_server_ptr.store(svr_.get());
    std::signal(SIGINT,  signal_handler);
    std::signal(SIGTERM, signal_handler);

    svr_->new_task_queue = [this] { return new httplib::ThreadPool(cfg_.thread_pool_size > 0 ? cfg_.thread_pool_size : 1); };
    svr_->set_logger([](const httplib::Request& req, const httplib::Response& res) {
        std::cout << "[" << get_timestamp() << "] " << req.remote_addr << " " << req.method << " " << req.path << " - " << res.status << std::endl;
    });
    svr_->Get(R"(/.*)", [this](const httplib::Request& r, httplib::Response& w) { handle_request_(r, w); });
    svr_->Options(R"(/.*)", [](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    std::cout << "========================================\n>> File Server Starting\n========================================\n"
              << "Address:    http://0.0.0.0:" << cfg_.port << "\nSource:     " << cfg_.root_path << "\n"
              << "Threads:    " << cfg_.thread_pool_size << "\nPage Size:  " << cfg_.page_size << " items\n"
              << "Symlinks:   " << (cfg_.follow_symlinks ? "Enabled" : "Disabled") << "\n========================================\nPress Ctrl+C to stop\n\n";
    if (!svr_->listen("0.0.0.0", cfg_.port)) return 1;
    return 0;
}