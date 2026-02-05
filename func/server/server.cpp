#include "server.h"
#include "httplib.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <vector>
#include <iostream>
#include <memory>
#include <array>
#include <unordered_map>
#include <chrono>
#include <ctime>
#include <csignal>
#include <atomic>
#include <thread>
#include <format>
namespace fs = std::filesystem;

namespace {
    // Global server pointer for signal handling
    std::atomic<httplib::Server*> g_server_ptr{nullptr};

    // Configuration
    struct Config {
        bool show_hidden = false;
        size_t page_size = 100;
        int thread_pool_size = 1;
        bool follow_symlinks = false;
    } g_config;

    // Signal handler for graceful shutdown
    void signal_handler(int signum) {
        std::cout << "\nReceived signal " << signum << ", shutting down gracefully..." << std::endl;
        httplib::Server* svr = g_server_ptr.load();
        if (svr) {
            svr->stop();
        }
    }

    // URL decode function
    std::string url_decode(const std::string& str) {
        std::string result;
        result.reserve(str.size());
        
        for (size_t i = 0; i < str.size(); ++i) {
            if (str[i] == '%' && i + 2 < str.size()) {
                try {
                    int value = std::stoi(str.substr(i + 1, 2), nullptr, 16);
                    result += static_cast<char>(value);
                    i += 2;
                } catch (...) {
                    result += str[i];
                }
            } else if (str[i] == '+') {
                result += ' ';
            } else {
                result += str[i];
            }
        }
        return result;
    }

    // HTML escape function
    std::string html_escape(const std::string& s) {
        std::string out;
        out.reserve(s.size() * 1.2);
        for (char c : s) {
            switch (c) {
                case '&': out += "&amp;"; break;
                case '<': out += "&lt;"; break;
                case '>': out += "&gt;"; break;
                case '"': out += "&quot;"; break;
                case '\'': out += "&#39;"; break; 
                default: out += c;
            }
        }
        return out;
    }

    // Format file size
    std::string format_size(uintmax_t bytes) {
        if (bytes == 0) return "0 B";
        const char* suffixes[] = {"B", "KB", "MB", "GB", "TB"};
        int s = 0;
        double count = static_cast<double>(bytes);
        while (count >= 1024 && s < 4) {
            s++;
            count /= 1024;
        }
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << count << " " << suffixes[s];
        return ss.str();
    }

    // Get current timestamp string
    std::string get_timestamp() {
        auto now = std::chrono::system_clock::now();
        auto time_t = std::chrono::system_clock::to_time_t(now);
        std::tm tm;
        #ifdef _WIN32
            localtime_s(&tm, &time_t);
        #else
            localtime_r(&time_t, &tm);
        #endif
        
        std::ostringstream ss;
        ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
        return ss.str();
    }

    // Generate breadcrumb navigation (Raw style)
    std::string generate_breadcrumbs(const std::string& url_path) {
        std::ostringstream html;
        html << "<div style='margin-bottom: 10px;'>";
        html << "<a href='/'>[Root]</a>";
        
        if (url_path != "/") {
            std::string accumulated = "";
            std::string remaining = url_path;
            
            if (!remaining.empty() && remaining[0] == '/') remaining = remaining.substr(1);
            if (!remaining.empty() && remaining.back() == '/') remaining.pop_back();
            
            size_t pos = 0;
            while ((pos = remaining.find('/')) != std::string::npos) {
                std::string part = remaining.substr(0, pos);
                accumulated += "/" + part;
                html << " / <a href='" << accumulated << "/'>[" << html_escape(part) << "]</a>";
                remaining = remaining.substr(pos + 1);
            }
            
            if (!remaining.empty()) {
                html << " / <b>[" << html_escape(remaining) << "]</b>";
            }
        }
        
        html << "</div>";
        return html.str();
    }

    // Custom error page (Raw style)
    std::string generate_error_page(int status, const std::string& message, const std::string& details = "") {
        std::ostringstream html;
        html << "<!DOCTYPE html>\n"
             << "<html lang=\"en\">\n"
             << "<head>\n"
             << "    <meta charset=\"UTF-8\">\n"
             << "    <title>Error " << status << "</title>\n"
             << "</head>\n"
             << "<body style=\"font-family: monospace; margin: 20px;\">\n"
             << "    <h1>Error " << status << ": " << html_escape(message) << "</h1>\n";
        
        if (!details.empty()) {
            html << "    <p><i>" << html_escape(details) << "</i></p>\n";
        }
        
        html << "    <hr>\n"
             << "    <p><a href=\"/\">[Back to Root]</a></p>\n"
             << "</body>\n"
             << "</html>\n";
        return html.str();
    }

    // Serve directory listing (Raw UI)
    void serve_directory(const fs::path& dir_path, const std::string& url_path, 
                        const std::unordered_map<std::string, std::string>& query_params,
                        httplib::Response& res) {
        
        // Parse pagination parameters
        size_t page = 1;
        size_t page_size = g_config.page_size;
        bool show_hidden = g_config.show_hidden;
        
        // Parse Sort parameters
        std::string sort_by = "name"; // default
        std::string sort_order = "asc"; // default

        auto page_it = query_params.find("page");
        if (page_it != query_params.end()) {
            try {
                page = std::max(1, std::stoi(page_it->second));
            } catch (...) {}
        }
        
        auto hidden_it = query_params.find("hidden");
        if (hidden_it != query_params.end()) {
            show_hidden = (hidden_it->second == "1" || hidden_it->second == "true");
        }

        auto sort_it = query_params.find("sort");
        if (sort_it != query_params.end()) {
            sort_by = sort_it->second;
        }

        auto order_it = query_params.find("order");
        if (order_it != query_params.end()) {
            sort_order = order_it->second;
        }

        const bool sort_desc = (sort_order == "desc");

        // Collect and filter entries
        std::vector<fs::directory_entry> entries;
        std::error_code dir_ec;
        
        for (auto& entry : fs::directory_iterator(dir_path, dir_ec)) {
            if (dir_ec) continue;
            
            std::string filename = entry.path().filename().string();
            
            // Filter hidden files
            if (!show_hidden && !filename.empty() && filename[0] == '.') {
                continue;
            }
            
            entries.push_back(entry);
        }

        // Sort: directories first, then by criteria
        std::sort(entries.begin(), entries.end(), [&](const fs::directory_entry& a, const fs::directory_entry& b) {
            bool a_is_dir = a.is_directory();
            bool b_is_dir = b.is_directory();
            
            // Always prioritize directories
            if (a_is_dir != b_is_dir) {
                return a_is_dir;
            }

            // Compare based on sort_by
            if (sort_by == "name") {
                std::string a_name = a.path().filename().string();
                std::string b_name = b.path().filename().string();
                return sort_desc ? (a_name > b_name) : (a_name < b_name);
            } 
                        else if (sort_by == "size") {
                std::error_code ec1, ec2;
                uintmax_t a_size = a_is_dir ? 0 : fs::file_size(a, ec1);
                uintmax_t b_size = b_is_dir ? 0 : fs::file_size(b, ec2);
                if (ec1) a_size = 0;
                if (ec2) b_size = 0;
                
                // If sizes are equal, fallback to name for stability
                if (a_size == b_size) {
                     std::string a_name = a.path().filename().string();
                     std::string b_name = b.path().filename().string();
                     return a_name < b_name;
                }
                return sort_desc ? (a_size > b_size) : (a_size < b_size);
            } 
            else if (sort_by == "time") {
                std::error_code ec1, ec2;
                auto a_time = fs::last_write_time(a.path(), ec1);
                auto b_time = fs::last_write_time(b.path(), ec2);
                if (ec1) a_time = fs::file_time_type::min(); 
                if (ec2) b_time = fs::file_time_type::min();
                
                // If times are equal, fallback to name
                if (a_time == b_time) {
                     std::string a_name = a.path().filename().string();
                     std::string b_name = b.path().filename().string();
                     return a_name < b_name;
                }
                return sort_desc ? (a_time > b_time) : (a_time < b_time);
            }
            else if (sort_by == "type") {
                // Type is just DIR vs FILE. Since we separated them above,
                // sorting by type within the groups essentially just sorts by name
                // or is redundant. We default to name sorting here.
                std::string a_name = a.path().filename().string();
                std::string b_name = b.path().filename().string();
                return sort_desc ? (a_name > b_name) : (a_name < b_name);
            }
            
            // Fallback
            return a.path().filename().string() < b.path().filename().string();
        });

        // Calculate pagination
        size_t total_entries = entries.size();
        size_t total_pages = (total_entries + page_size - 1) / page_size;
        size_t start_idx = (page - 1) * page_size;
        size_t end_idx = std::min(start_idx + page_size, total_entries);

        // C++23: Helper to convert file_time to string using <format> and clock_cast
        auto format_file_time = [](const fs::path& p) -> std::string {
            std::error_code ec;
            auto ftime = fs::last_write_time(p, ec);
            if (ec) return "-";
            
            try {
                // C++20/23: Use clock_cast to convert filesystem time to system time accurately
                auto sys_time = std::chrono::clock_cast<std::chrono::system_clock>(ftime);
                // C++20/23: Use std::format for safe, type-safe formatting
                return std::format("{:%Y-%m-%d %H:%M}", sys_time);
            } catch (...) {
                return "-";
            }
        };

        // Helper to generate sort links
        auto get_sort_header = [&](const std::string& column, const std::string& label) -> std::string {
            std::string next_order = "asc";
            std::string arrow = "";
            
            if (sort_by == column) {
                if (sort_desc) {
                    next_order = "asc";
                    arrow = " &#8595;"; // Down arrow
                } else {
                    next_order = "desc";
                    arrow = " &#8593;"; // Up arrow
                }
            }
            
            std::string js_call = "setSort('" + column + "', '" + next_order + "')";
            return "<a href=\"javascript:void(0)\" onclick=\"" + js_call + "\">" + label + arrow + "</a>";
        };

        // Generate Raw HTML
        std::ostringstream html;
        html << "<!DOCTYPE html>\n"
             << "<html lang=\"en\">\n"
             << "<head>\n"
             << "    <meta charset=\"UTF-8\">\n"
             << "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
             << "    <title>Index of " << html_escape(url_path) << "</title>\n"
             << "    <style>\n"
             << "        body { font-family: monospace; margin: 20px; }\n"
             << "        a { color: #0000EE; text-decoration: none; }\n"
             << "        a:hover { text-decoration: underline; }\n"
             << "        th a { color: #000; font-weight: bold; display: block; }\n"
             << "        table { border-collapse: collapse; width: 100%; }\n"
             << "        th, td { border: 1px solid #ccc; padding: 5px; }\n"
             << "        th { background-color: #efefef; text-align: left; }\n"
             << "        .btn { cursor: pointer; } \n"
             << "    </style>\n"
             << "</head>\n"
             << "<body>\n"
             << "    <h1>Index of " << html_escape(url_path) << "</h1>\n"
             << generate_breadcrumbs(url_path)
             << "    <hr>\n"
             << "    <div style=\"margin-bottom: 15px;\">\n";
        
        // Parent directory button
        if (url_path != "/") {
            std::string parent_path = url_path;
            if (!parent_path.empty() && parent_path.back() == '/') parent_path.pop_back();
            size_t last_slash = parent_path.find_last_of('/');
            parent_path = (last_slash != std::string::npos) ? parent_path.substr(0, last_slash + 1) : "/";
            html << "        <button class=\"btn\" onclick=\"window.location.href='" << parent_path << "'\">[Parent Directory]</button>\n";
        }
        
        html << "        <button class=\"btn\" onclick=\"window.location.reload()\">[Refresh]</button>\n"
             << "        <button class=\"btn\" id=\"btnHidden\" onclick=\"toggleHidden()\">[Show Hidden]</button>\n"
             << "    </div>\n"
             << "    <hr>\n"
             << "    <table>\n"
             << "        <thead>\n"
             << "            <tr>\n"
             << "                <th style=\"width: 40%;\">" << get_sort_header("name", "Name") << "</th>\n"
             << "                <th style=\"width: 80px;\">Type</th>\n"
             << "                <th style=\"width: 120px;\">" << get_sort_header("size", "Size") << "</th>\n"
             << "                <th style=\"width: 160px;\">" << get_sort_header("time", "Modified") << "</th>\n"
             << "                <th style=\"width: 100px;\">Action</th>\n"
             << "            </tr>\n"
             << "        </thead>\n"
             << "        <tbody>\n";

        if (entries.empty()) {
            html << "            <tr><td colspan=\"5\" style=\"text-align:center;\">Empty Directory</td></tr>\n";
        } else {
            for (size_t i = start_idx; i < end_idx; ++i) {
                auto& entry = entries[i];
                std::string name = entry.path().filename().string();
                std::string safe_name = html_escape(name);
                std::string link = url_path + name;
                
                bool is_dir = entry.is_directory();
                if (is_dir) link += "/";

                std::string size_str = "-";
                std::string time_str = format_file_time(entry.path());
                
                if (!is_dir) {
                    std::error_code ec;
                    auto size = fs::file_size(entry.path(), ec);
                    if (!ec) size_str = format_size(size);
                }

                std::string icon = is_dir ? "[DIR]" : "[FILE]";

                html << "            <tr>";
                html << "<td>" << icon << " ";
                if (is_dir) {
                    html << "<b><a href=\"" << link << "\">" << safe_name << "/</a></b>";
                } else {
                    html << "<a href=\"" << link << "\">" << safe_name << "</a>";
                }
                html << "</td>";
                html << "<td>" << (is_dir ? "DIR" : "FILE") << "</td>";
                html << "<td>" << size_str << "</td>";
                html << "<td>" << time_str << "</td>";
                if (is_dir) {
                    html << "<td><button class=\"btn\" onclick=\"window.location.href='" << link << "'\">[Open]</button></td>";
                } else {
                    html << "<td><a href=\"" << link << "\" download>[Download]</a></td>";
                }
                html << "</tr>\n";
            }
        }

        html << "        </tbody>\n"
             << "    </table>\n";

        // Pagination
        if (total_pages > 1) {
            html << "    <hr>\n"
                 << "    <div style=\"text-align: center; margin-top: 10px;\">\n";
            
            if (page > 1) {
                html << "        <a href=\"javascript:void(0)\" onclick=\"goToPage(" << (page - 1) << ")\">[&lt; Prev]</a> \n";
            } else {
                html << "        <span style=\"color: gray;\">[&lt; Prev]</span> \n";
            }
            
            html << "        Page " << page << " of " << total_pages 
                 << " (" << total_entries << " items) \n";
            
            if (page < total_pages) {
                html << "        <a href=\"javascript:void(0)\" onclick=\"goToPage(" << (page + 1) << ")\">[Next &gt;]</a>\n";
            } else {
                html << "        <span style=\"color: gray;\">[Next &gt;]</span>\n";
            }
            
            html << "    </div>\n";
        }

        html << "    <script>\n"
             << "        function updateHiddenButton() {\n"
             << "            const url = new URL(window.location);\n"
             << "            const isHidden = url.searchParams.get('hidden') === '1';\n"
             << "            const btn = document.getElementById('btnHidden');\n"
             << "            if (btn) {\n"
             << "                btn.innerText = isHidden ? \"[Hide Hidden]\" : \"[Show Hidden]\";\n"
             << "            }\n"
             << "        }\n"
             << "        function toggleHidden() {\n"
             << "            const url = new URL(window.location);\n"
             << "            const isHidden = url.searchParams.get('hidden') === '1';\n"
             << "            if (isHidden) {\n"
             << "                url.searchParams.delete('hidden');\n"
             << "            } else {\n"
             << "                url.searchParams.set('hidden', '1');\n"
             << "            }\n"
             << "            url.searchParams.delete('page');\n"
             << "            window.location.href = url.toString();\n"
             << "        }\n"
             << "        function setSort(column, order) {\n"
             << "            const url = new URL(window.location);\n"
             << "            url.searchParams.set('sort', column);\n"
             << "            url.searchParams.set('order', order);\n"
             << "            url.searchParams.delete('page');\n"
             << "            window.location.href = url.toString();\n"
             << "        }\n"
             << "        function goToPage(page) {\n"
             << "            const url = new URL(window.location);\n"
             << "            url.searchParams.set('page', page);\n"
             << "            window.location.href = url.toString();\n"
             << "        }\n"
             << "        updateHiddenButton();\n"
             << "    </script>\n"
             << "</body>\n"
             << "</html>\n";

        res.set_content(html.str(), "text/html");
    }
}

// Updated signature to accept page_size and thread_pool_size
int start_server(const std::string& root_path, int port, size_t page_size, int thread_pool_size, bool symlinks_enabled) {
    // Update global config
    g_config.page_size = page_size;
    // Ensure at least 1 thread
    g_config.thread_pool_size = (thread_pool_size > 0) ? thread_pool_size : 1; 
    g_config.follow_symlinks = symlinks_enabled;

    httplib::Server svr;

    // Setup signal handlers for graceful shutdown
    g_server_ptr.store(&svr);
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // Resolve and validate root path
    std::error_code ec;
    fs::path real_root = fs::canonical(root_path, ec);
    if (ec) {
        real_root = fs::absolute(root_path).lexically_normal();
    } else {
        real_root = real_root.lexically_normal();
    }

    if (!fs::exists(real_root) || !fs::is_directory(real_root)) {
        std::cerr << "Error: Directory does not exist: " << real_root << std::endl;
        return 1;
    }

    // Configure thread pool
    svr.new_task_queue = [&] { 
        return new httplib::ThreadPool(g_config.thread_pool_size); 
    };

    // Logger
    svr.set_logger([](const httplib::Request& req, const httplib::Response& res) {
        std::cout << "[" << get_timestamp() << "] "
                  << req.remote_addr << " "
                  << req.method << " " << req.path 
                  << " - " << res.status << std::endl;
    });

    // Path resolution helper with proper security checks
    auto resolve_safe_path = [&real_root](const std::string& url_path) -> std::string {
        // URL decode first
        std::string decoded = url_decode(url_path);
        
        // Remove leading slash
        std::string rel_url = decoded;
        if (!rel_url.empty() && rel_url[0] == '/') {
            rel_url.erase(0, 1);
        }

        // Combine and normalize
        fs::path combined = (real_root / fs::path(rel_url)).lexically_normal();

        // Security check: ensure the path is within root
        std::string combined_str = combined.string();
        std::string root_str = real_root.string();
        
        // On Windows, preferred_separator is wchar_t, but we are using std::string (char).
        // We need to cast it to char for string operations.
        const char path_sep = static_cast<char>(fs::path::preferred_separator);

        // Ensure root_str ends with separator for accurate prefix matching
        if (!root_str.empty() && root_str.back() != path_sep) {
            root_str += path_sep;
        }
        if (!combined_str.empty() && combined_str.back() != path_sep 
            && fs::is_directory(combined)) {
            combined_str += path_sep;
        }

        // Check if combined path starts with root path
        if (combined_str.size() < root_str.size() || 
            combined_str.substr(0, root_str.size()) != root_str) {
            // Allow exact match with root
            if (combined_str + path_sep != root_str) {
                return ""; // Path traversal detected
            }
        }

        return combined.string();
    };

    // Main request handler
    svr.Get(R"(/.*)", [&](const httplib::Request& req, httplib::Response& res) {
        // Set CORS headers
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");

        std::string url_path = req.path;
        std::string fs_path = resolve_safe_path(url_path);

        if (fs_path.empty()) {
            res.status = 403;
            res.set_content(generate_error_page(403, "Forbidden", "Access to this path is not allowed"), "text/html");
            return;
        }

        fs::path p = fs::path(fs_path);

        // Check if path exists
        std::error_code exists_ec;
        bool exists = fs::exists(p, exists_ec);
        
        if (exists_ec || !exists) {
            res.status = 404;
            res.set_content(generate_error_page(404, "Not Found", "The requested resource could not be found"), "text/html");
            return;
        }

        // Handle symlinks based on config
        if (!g_config.follow_symlinks) {
            std::error_code ec;
            
            // 1. Get the physical path (resolves all symlinks)
            fs::path canonical_path = fs::canonical(p, ec);

            if (!ec) {
                // Helper lambda to remove trailing separators for fair comparison
                auto normalize_path_str = [](const std::string& path) -> std::string {
                    std::string res = path;
                    const char sep = static_cast<char>(fs::path::preferred_separator);
                    // Remove trailing slashes
                    while (!res.empty() && res.back() == sep) {
                        res.pop_back();
                    }
                    return res;
                };

                // Compare normalized strings
                std::string req_str = normalize_path_str(p.string());
                std::string real_str = normalize_path_str(canonical_path.string());

                if (req_str != real_str) {
                    res.status = 403;
                    res.set_content(generate_error_page(403, "Forbidden", "Symbolic links are not followed"), "text/html");
                    return;
                }
            } else {
                // Fallback for broken symlinks (where canonical might fail)
                if (fs::is_symlink(p, ec) && !ec) {
                    res.status = 403;
                    res.set_content(generate_error_page(403, "Forbidden", "Symbolic links are not followed"), "text/html");
                    return;
                }
            }
        }

        // Handle directories
        if (fs::is_directory(p)) {
            if (url_path.back() != '/') {
                res.set_redirect(url_path + "/");
                return;
            }
            
            // FIX: httplib parses query strings into req.params automatically.
            // We use req.params instead of manually parsing req.path.
            std::unordered_map<std::string, std::string> query_params;
            for (const auto& param : req.params) {
                query_params[param.first] = param.second;
            }
            
            serve_directory(p, url_path, query_params, res);
            return;
        }

        else {
            std::error_code size_ec;
            uintmax_t file_size = fs::file_size(p, size_ec);
            if (size_ec) {
                res.status = 500;
                res.set_content("Could not determine file size", "text/plain");
                return;
            }

            // 1. Parse Range Header
            std::string range_header = req.get_header_value("Range");
            uintmax_t start = 0;
            uintmax_t end = file_size - 1;
            bool is_range = false;

            if (!range_header.empty()) {
                if (range_header.find("bytes=") == 0) {
                    std::string range_spec = range_header.substr(6);
                    size_t dash_pos = range_spec.find('-');
                    if (dash_pos != std::string::npos) {
                        try {
                            std::string s_start = range_spec.substr(0, dash_pos);
                            std::string s_end = range_spec.substr(dash_pos + 1);
                            if (!s_start.empty()) start = std::stoull(s_start);
                            if (!s_end.empty()) end = std::stoull(s_end);
                            else end = file_size - 1;
                            if (start < file_size && end < file_size && start <= end) {
                                is_range = true;
                            }
                        } catch (...) { /* Ignore invalid range */ }
                    }
                }
            }

            uintmax_t content_length = is_range ? (end - start + 1) : file_size;

            // 2. Open File
            auto file_ptr = std::make_shared<std::ifstream>(p, std::ios::binary);
            if (!file_ptr->is_open()) {
                res.status = 500;
                res.set_content("Failed to open file", "text/plain");
                return;
            }

            // 3. Set Headers
            if (is_range) {
                res.status = 206;
                std::string content_range = "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(file_size);
                res.set_header("Content-Range", content_range.c_str());
            } else {
                res.status = 200;
                res.set_header("Accept-Ranges", "bytes");
            }

            // 4. Stream
            res.set_content_provider(
                content_length,
                "application/octet-stream",
                [file_ptr, start](size_t offset, size_t length, httplib::DataSink &sink) {
                    uintmax_t file_pos = start + offset;

                    file_ptr->clear();
                    file_ptr->seekg(file_pos);
                    
                    if (!file_ptr->good()) return false;

                    const size_t chunk_size = 64 * 1024; 
                    std::array<char, chunk_size> buffer; 

                    size_t to_read = std::min(length, chunk_size);
                    
                    file_ptr->read(buffer.data(), to_read);
                    size_t read_count = file_ptr->gcount();

                    if (read_count > 0) {
                        sink.write(buffer.data(), read_count);
                    }
                    
                    return true;
                }
            );
        }
    });

    // Handle OPTIONS for CORS preflight
    svr.Options(R"(/.*)", [](const httplib::Request& /*req*/, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type");
        res.status = 204;
    });

    std::cout << "========================================" << std::endl;
    std::cout << ">> File Server Starting" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Address:    http://0.0.0.0:" << port << std::endl;
    std::cout << "Directory:  " << real_root << std::endl;
    std::cout << "Threads:    " << g_config.thread_pool_size << std::endl;
    std::cout << "Page Size:  " << g_config.page_size << " items" << std::endl;
    std::cout << "Symlinks:   " << (g_config.follow_symlinks ? "Enabled" : "Disabled") << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Press Ctrl+C to stop the server" << std::endl;
    std::cout << std::endl;

    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "Error: Failed to start server on port " << port << std::endl;
        return 1;
    }

    std::cout << "Server stopped gracefully." << std::endl;
    return 0;
}