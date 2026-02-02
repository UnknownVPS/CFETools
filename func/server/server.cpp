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
namespace fs = std::filesystem;

namespace {
    // Global server pointer for signal handling
    std::atomic<httplib::Server*> g_server_ptr{nullptr};
    unsigned int n = std::thread::hardware_concurrency();

    // Configuration
    struct Config {
        bool show_hidden = false;
        size_t page_size = 100;
        int thread_pool_size = n;
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

    // MIME type detection
    std::string get_mime_type(const fs::path& path) {
        static const std::unordered_map<std::string, std::string> ext_map = {
            {".html", "text/html"},
            {".htm", "text/html"},
            {".css", "text/css"},
            {".js", "application/javascript"},
            {".json", "application/json"},
            {".xml", "application/xml"},
            {".png", "image/png"},
            {".jpg", "image/jpeg"},
            {".jpeg", "image/jpeg"},
            {".gif", "image/gif"},
            {".svg", "image/svg+xml"},
            {".ico", "image/x-icon"},
            {".webp", "image/webp"},
            {".pdf", "application/pdf"},
            {".mp4", "video/mp4"},
            {".webm", "video/webm"},
            {".ogg", "video/ogg"},
            {".mp3", "audio/mpeg"},
            {".wav", "audio/wav"},
            {".zip", "application/zip"},
            {".tar", "application/x-tar"},
            {".gz", "application/gzip"},
            {".7z", "application/x-7z-compressed"},
            {".rar", "application/x-rar-compressed"},
            {".txt", "text/plain"},
            {".md", "text/markdown"},
            {".csv", "text/csv"},
            {".doc", "application/msword"},
            {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
            {".xls", "application/vnd.ms-excel"},
            {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
            {".ppt", "application/vnd.ms-powerpoint"},
            {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
            {".wasm", "application/wasm"},
        };
        
        std::string ext = path.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        auto it = ext_map.find(ext);
        return (it != ext_map.end()) ? it->second : "application/octet-stream";
    }

    // Generate ETag from file metadata
    std::string generate_etag(const fs::path& path) {
        std::error_code ec;
        auto file_size = fs::file_size(path, ec);
        if (ec) return "";
        
        auto last_write = fs::last_write_time(path, ec);
        if (ec) return "";
        
        auto time_since_epoch = last_write.time_since_epoch().count();
        
        std::ostringstream ss;
        // Cast time_since_epoch to unsigned long long to support platforms where it is __int128 (e.g. Android NDK)
        ss << "\"" << std::hex << file_size << "-" << static_cast<unsigned long long>(time_since_epoch) << "\"";
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

    // Parse query parameters
    std::unordered_map<std::string, std::string> parse_query(const std::string& query) {
        std::unordered_map<std::string, std::string> params;
        if (query.empty()) return params;
        
        size_t start = 0;
        while (start < query.size()) {
            size_t eq = query.find('=', start);
            size_t amp = query.find('&', start);
            
            if (eq == std::string::npos) break;
            
            std::string key = query.substr(start, eq - start);
            std::string value;
            
            if (amp == std::string::npos) {
                value = query.substr(eq + 1);
                start = query.size();
            } else {
                value = query.substr(eq + 1, amp - eq - 1);
                start = amp + 1;
            }
            
            params[url_decode(key)] = url_decode(value);
        }
        
        return params;
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

        // Sort: directories first, then alphabetically
        std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
            bool a_is_dir = a.is_directory();
            bool b_is_dir = b.is_directory();
            if (a_is_dir == b_is_dir) {
                return a.path().filename().string() < b.path().filename().string();
            }
            return a_is_dir;
        });

        // Calculate pagination
        size_t total_entries = entries.size();
        size_t total_pages = (total_entries + page_size - 1) / page_size;
        size_t start_idx = (page - 1) * page_size;
        size_t end_idx = std::min(start_idx + page_size, total_entries);

        // Generate Raw HTML
        std::ostringstream html;
        html << "<!DOCTYPE html>\n"
             << "<html lang=\"en\">\n"
             << "<head>\n"
             << "    <meta charset=\"UTF-8\">\n"
             << "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
             << "    <title>Index of " << html_escape(url_path) << "</title>\n"
             << "</head>\n"
             << "<body style=\"font-family: monospace; margin: 20px;\">\n"
             << "    <h1>Index of " << html_escape(url_path) << "</h1>\n"
             << generate_breadcrumbs(url_path)
             << "    <hr>\n"
             << "    <form style=\"margin-bottom: 15px;\">\n";
        
        // Parent directory button
        if (url_path != "/") {
            std::string parent_path = url_path;
            if (!parent_path.empty() && parent_path.back() == '/') parent_path.pop_back();
            size_t last_slash = parent_path.find_last_of('/');
            parent_path = (last_slash != std::string::npos) ? parent_path.substr(0, last_slash + 1) : "/";
            html << "        <button type=\"button\" onclick=\"window.location.href='" << parent_path << "'\">[Parent Directory]</button>\n";
        }
        
        html << "        <button type=\"button\" onclick=\"window.location.reload()\">[Refresh]</button>\n"
             << "        <button type=\"button\" id=\"btnHidden\" onclick=\"toggleHidden()\">[Show Hidden]</button>\n"
             << "    </form>\n"
             << "    <hr>\n"
             << "    <table border=\"1\" cellpadding=\"5\" cellspacing=\"0\" width=\"100%\">\n"
             << "        <thead>\n"
             << "            <tr style=\"background-color: #efefef;\">\n"
             << "                <th style=\"text-align: left;\">Name</th>\n"
             << "                <th style=\"width: 100px;\">Type</th>\n"
             << "                <th style=\"width: 120px;\">Size</th>\n"
             << "                <th style=\"width: 120px;\">Action</th>\n"
             << "            </tr>\n"
             << "        </thead>\n"
             << "        <tbody>\n";

        if (entries.empty()) {
            html << "            <tr><td colspan=\"4\" style=\"text-align:center;\">Empty Directory</td></tr>\n";
        } else {
            for (size_t i = start_idx; i < end_idx; ++i) {
                auto& entry = entries[i];
                std::string name = entry.path().filename().string();
                std::string safe_name = html_escape(name);
                std::string link = url_path + name;
                
                bool is_dir = entry.is_directory();
                if (is_dir) link += "/";

                std::string size_str = "-";
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
                if (is_dir) {
                    html << "<td><button onclick=\"window.location.href='" << link << "'\">[Open]</button></td>";
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
             << "        function goToPage(page) {\n"
             << "            const url = new URL(window.location);\n"
             << "            url.searchParams.set('page', page);\n"
             << "            window.location.href = url.toString();\n"
             << "        }\n"
             << "        // Initialize UI state on load\n"
             << "        updateHiddenButton();\n"
             << "    </script>\n"
             << "</body>\n"
             << "</html>\n";

        res.set_content(html.str(), "text/html");
    }
}

int start_server(const std::string& root_path, int port) {
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
        std::error_code symlink_ec;
        bool is_symlink = fs::is_symlink(p, symlink_ec);
        
        if (!symlink_ec && is_symlink && !g_config.follow_symlinks) {
            res.status = 403;
            res.set_content(generate_error_page(403, "Forbidden", "Symbolic links are not followed"), "text/html");
            return;
        }

        // Handle directories
        if (fs::is_directory(p)) {
            if (url_path.back() != '/') {
                res.set_redirect(url_path + "/");
                return;
            }
            
            auto query_params = parse_query(req.get_header_value("Query-String"));
            // Also parse from URL if httplib doesn't provide it
            size_t query_pos = req.path.find('?');
            if (query_pos != std::string::npos) {
                query_params = parse_query(req.path.substr(query_pos + 1));
            }
            
            serve_directory(p, url_path, query_params, res);
            return;
        }

        // Handle files
        std::error_code size_ec;
        uintmax_t file_size = fs::file_size(p, size_ec);
        if (size_ec) {
            res.status = 500;
            res.set_content(generate_error_page(500, "Internal Server Error", "Could not determine file size"), "text/html");
            return;
        }

        // Generate ETag
        std::string etag = generate_etag(p);
        
        // Check If-None-Match header for caching
        std::string if_none_match = req.get_header_value("If-None-Match");
        if (!etag.empty() && !if_none_match.empty() && if_none_match == etag) {
            res.status = 304;
            res.set_header("ETag", etag.c_str());
            return;
        }

        // Determine MIME type
        std::string mime_type = get_mime_type(p);

        // Parse Range header
        std::string range_header = req.get_header_value("Range");
        uintmax_t start = 0;
        uintmax_t end = file_size - 1;
        bool is_range = false;

        if (!range_header.empty() && range_header.find("bytes=") == 0) {
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
                } catch (...) {
                    // Invalid range, ignore
                }
            }
        }

        uintmax_t content_length = is_range ? (end - start + 1) : file_size;

        // Open file
        auto file_ptr = std::make_shared<std::ifstream>(p, std::ios::binary);
        if (!file_ptr->is_open()) {
            res.status = 500;
            res.set_content(generate_error_page(500, "Internal Server Error", "Failed to open file"), "text/html");
            return;
        }

        // Seek to start position once
        file_ptr->seekg(start);
        if (!file_ptr->good()) {
            res.status = 500;
            res.set_content(generate_error_page(500, "Internal Server Error", "Failed to seek in file"), "text/html");
            return;
        }

        // Set response headers
        if (is_range) {
            res.status = 206;
            std::ostringstream content_range;
            content_range << "bytes " << start << "-" << end << "/" << file_size;
            res.set_header("Content-Range", content_range.str().c_str());
        } else {
            res.status = 200;
            res.set_header("Accept-Ranges", "bytes");
        }

        if (!etag.empty()) {
            res.set_header("ETag", etag.c_str());
        }

        res.set_header("Cache-Control", "public, max-age=3600");

        // Create shared buffer for streaming (reused across callbacks)
        auto buffer = std::make_shared<std::vector<char>>(64 * 1024);
        auto bytes_remaining = std::make_shared<uintmax_t>(content_length);

        // Stream file content
        res.set_content_provider(
            content_length,
            mime_type.c_str(),
            [file_ptr, buffer, bytes_remaining](size_t offset, size_t length, httplib::DataSink &sink) {
                // Don't seek on every call - file pointer advances naturally
                uintmax_t remaining = *bytes_remaining;
                if (remaining == 0) return false;

                size_t to_read = std::min(static_cast<size_t>(remaining), buffer->size());
                to_read = std::min(to_read, length);

                file_ptr->read(buffer->data(), to_read);
                std::streamsize read_count = file_ptr->gcount();

                if (read_count > 0) {
                    *bytes_remaining -= read_count;
                    return sink.write(buffer->data(), read_count);
                }

                return false;
            }
        );
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