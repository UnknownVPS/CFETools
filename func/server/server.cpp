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
#include <map>
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
        bool is_cfup_mode = false;
    } g_config;

    // --- CFUP Support Structures ---
    struct CfupNode {
        bool is_dir = true;
        uint64_t offset = 0;
        uint64_t size = 0;
        std::map<std::string, std::shared_ptr<CfupNode>> children;
    };
    
    std::shared_ptr<CfupNode> g_cfup_root;
    std::string g_cfup_file_path;
    fs::file_time_type g_cfup_time_val;
    std::string g_cfup_time_str;

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

    // File time to string converter
    std::string time_to_string(fs::file_time_type ftime) {
        try {
            auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                ftime - fs::file_time_type::clock::now() + std::chrono::system_clock::now()
            );
            
            auto sys_time_t = std::chrono::system_clock::to_time_t(sctp);
            std::tm tm;
            #ifdef _WIN32
                localtime_s(&tm, &sys_time_t);
            #else
                localtime_r(&sys_time_t, &tm);
            #endif
            
            std::ostringstream oss;
            oss << std::put_time(&tm, "%Y-%m-%d %H:%M");
            return oss.str();
        } catch (...) {
            return "-";
        }
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

    // Virtual Directory Entry format for uniform logic
    struct VDirEntry {
        std::string name;
        bool is_dir = false;
        uintmax_t size = 0;
        fs::file_time_type time_val;
        std::string time_str;
    };

    // Fast memory index loader for CFUP files
    bool load_cfup_index(const std::string& filepath) {
        std::error_code fs_ec;
        uintmax_t file_size_total = fs::file_size(filepath, fs_ec);
        if (fs_ec) {
            std::cerr << "[CFUP Error] Failed to get total file size: " << fs_ec.message() << std::endl;
            return false;
        }

        std::ifstream in(filepath, std::ios::binary);
        if (!in) {
            std::cerr << "[CFUP Error] Failed to open file stream." << std::endl;
            return false;
        }
        
        uint32_t file_count = 0;
        if (!in.read(reinterpret_cast<char*>(&file_count), sizeof(file_count))) {
            std::cerr << "[CFUP Error] Failed to read initial file count." << std::endl;
            return false;
        }
        
        // Basic sanity check to avoid OOM
        if (file_count > 10000000) { 
            std::cerr << "[CFUP Error] Sanity check failed: file_count=" << file_count << " is suspiciously large." << std::endl;
            return false; 
        }
        
        g_cfup_root = std::make_shared<CfupNode>();
        g_cfup_file_path = filepath;
        
        std::error_code ec;
        g_cfup_time_val = fs::last_write_time(filepath, ec);
        g_cfup_time_str = ec ? "-" : time_to_string(g_cfup_time_val);
        
        size_t successful_files = 0;

        for (uint32_t i = 0; i < file_count; ++i) {
            uint32_t path_len = 0;
            if (!in.read(reinterpret_cast<char*>(&path_len), sizeof(path_len))) {
                std::cerr << "[CFUP Warning] Unexpected EOF reading path length at item " << i << ". Archive may be truncated." << std::endl;
                break;
            }
            
            // Length Sanity Check
            if (path_len == 0 || path_len > 65536) { 
                std::cerr << "[CFUP Warning] Invalid path length (" << path_len << ") at item index " << i << ". Stopping parse." << std::endl;
                break; 
            }
            
            std::string rel_path(path_len, '\0');
            if (!in.read(rel_path.data(), path_len)) {
                std::cerr << "[CFUP Warning] Failed reading path string at item index " << i << ". Archive may be truncated." << std::endl;
                break;
            }
            
            uint64_t data_size = 0;
            if (!in.read(reinterpret_cast<char*>(&data_size), sizeof(data_size))) {
                std::cerr << "[CFUP Warning] Failed reading data payload size at item index " << i << ". Archive may be truncated." << std::endl;
                break;
            }
            
            uint64_t offset = in.tellg();
            if (offset == static_cast<uint64_t>(-1)) {
                std::cerr << "[CFUP Error] Stream tellg() failed at item index " << i << ". Possible 32-bit limitation or broken stream state." << std::endl;
                break;
            }

            if (offset + data_size > file_size_total) {
                std::cerr << "\n[CFUP Warning] Truncation safely triggered!\n"
                          << "-> Item " << i << " (" << rel_path << ") requires " << data_size << " bytes.\n"
                          << "-> But only " << (file_size_total - offset) << " bytes remain in the file.\n"
                          << "-> The archive file on disk is incomplete (likely a failed download or copy).\n"
                          << "-> Mounting in 'Best-Effort' mode. Proceeding with the " << successful_files << " valid files found so far...\n" << std::endl;
                break; // Stop parsing, but keep what we have
            }
            
            // Register path in hierarchy
            std::string remaining = rel_path;
            std::replace(remaining.begin(), remaining.end(), '\\', '/'); // Standardize slashes
            
            auto curr = g_cfup_root;
            size_t pos = 0;
            while ((pos = remaining.find('/')) != std::string::npos) {
                std::string part = remaining.substr(0, pos);
                remaining = remaining.substr(pos + 1);
                if (part.empty() || part == ".") continue;
                
                if (curr->children.find(part) == curr->children.end()) {
                    curr->children[part] = std::make_shared<CfupNode>();
                }
                curr = curr->children[part];
            }
            if (!remaining.empty()) {
                auto leaf = std::make_shared<CfupNode>();
                leaf->is_dir = false;
                leaf->offset = offset;
                leaf->size = data_size;
                curr->children[remaining] = leaf;
            }
            
            successful_files++;
            
            // Skip directly over the data payload in O(1) time
            in.seekg(offset + data_size, std::ios::beg);
            
            // Attempt to clear EOF if we gracefully landed precisely at the end of the file mid-loop
            if (!in.good() && i != file_count - 1) {
                in.clear();
            }
        }
        
        if (successful_files == 0) {
            std::cerr << "[CFUP Error] Archive is completely invalid. No files could be parsed." << std::endl;
            return false;
        }

        return true;
    }

    // Serve directory listing (Raw UI adapted for Generic Entries)
    void serve_directory_generic(const std::vector<VDirEntry>& raw_entries, const std::string& url_path, 
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

        // Filter entries
        std::vector<VDirEntry> entries;
        for (const auto& entry : raw_entries) {
            if (!show_hidden && !entry.name.empty() && entry.name[0] == '.') {
                continue;
            }
            entries.push_back(entry);
        }

        // Sort: directories first, then by criteria
        std::sort(entries.begin(), entries.end(), [&](const VDirEntry& a, const VDirEntry& b) {
            // Always prioritize directories
            if (a.is_dir != b.is_dir) {
                return a.is_dir;
            }

            // Compare based on sort_by
            if (sort_by == "name" || sort_by == "type") {
                return sort_desc ? (a.name > b.name) : (a.name < b.name);
            } 
            else if (sort_by == "size") {
                if (a.size == b.size) return a.name < b.name; // Fallback
                return sort_desc ? (a.size > b.size) : (a.size < b.size);
            } 
            else if (sort_by == "time") {
                if (a.time_val == b.time_val) return a.name < b.name; // Fallback
                return sort_desc ? (a.time_val > b.time_val) : (a.time_val < b.time_val);
            }
            
            // Fallback
            return a.name < b.name;
        });

        // Calculate pagination
        size_t total_entries = entries.size();
        size_t total_pages = (total_entries + page_size - 1) / page_size;
        size_t start_idx = (page - 1) * page_size;
        size_t end_idx = std::min(start_idx + page_size, total_entries);

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
                std::string safe_name = html_escape(entry.name);
                std::string link = url_path + entry.name;
                
                if (entry.is_dir) link += "/";

                std::string size_str = entry.is_dir ? "-" : format_size(entry.size);
                std::string icon = entry.is_dir ? "[DIR]" : "[FILE]";

                html << "            <tr>";
                html << "<td>" << icon << " ";
                if (entry.is_dir) {
                    html << "<b><a href=\"" << link << "\">" << safe_name << "/</a></b>";
                } else {
                    html << "<a href=\"" << link << "\">" << safe_name << "</a>";
                }
                html << "</td>";
                html << "<td>" << (entry.is_dir ? "DIR" : "FILE") << "</td>";
                html << "<td>" << size_str << "</td>";
                html << "<td>" << entry.time_str << "</td>";
                if (entry.is_dir) {
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
    g_config.is_cfup_mode = false;

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

    if (!fs::exists(real_root)) {
        std::cerr << "Error: Target path does not exist: " << real_root << std::endl;
        return 1;
    }

    if (fs::is_regular_file(real_root)) {
        std::cout << "Detected file input. Attempting to parse as CFUP archive..." << std::endl;
        if (load_cfup_index(real_root.string())) {
            g_config.is_cfup_mode = true;
            std::cout << "Successfully parsed CFUP archive index." << std::endl;
        } else {
            std::cerr << "Error: File provided is not a directory or a valid CFUP archive." << std::endl;
            return 1;
        }
    } else if (!fs::is_directory(real_root)) {
        std::cerr << "Error: Path is neither a directory nor a regular file." << std::endl;
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

    // Path resolution helper with proper security checks (For Normal FS Mode)
    auto resolve_safe_path = [&real_root](const std::string& url_path) -> std::string {
        std::string decoded = url_decode(url_path);
        std::string rel_url = decoded;
        if (!rel_url.empty() && rel_url[0] == '/') rel_url.erase(0, 1);

        fs::path combined = (real_root / fs::path(rel_url)).lexically_normal();

        std::string combined_str = combined.string();
        std::string root_str = real_root.string();
        
        const char path_sep = static_cast<char>(fs::path::preferred_separator);

        if (!root_str.empty() && root_str.back() != path_sep) {
            root_str += path_sep;
        }
        if (!combined_str.empty() && combined_str.back() != path_sep 
            && fs::is_directory(combined)) {
            combined_str += path_sep;
        }

        if (combined_str.size() < root_str.size() || 
            combined_str.substr(0, root_str.size()) != root_str) {
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
        
        std::unordered_map<std::string, std::string> query_params;
        for (const auto& param : req.params) {
            query_params[param.first] = param.second;
        }

        // --- CFUP ARCHIVE HANDLING ---
        if (g_config.is_cfup_mode) {
            std::string decoded = url_decode(url_path);
            
            // Path traversal guard (CFUP is locked environment)
            if (decoded.find("..") != std::string::npos) {
                res.status = 403;
                res.set_content(generate_error_page(403, "Forbidden", "Invalid path traversal"), "text/html");
                return;
            }

            // Traverse CFUP Virtual Tree
            auto curr = g_cfup_root;
            std::string remaining = decoded;
            
            if (!remaining.empty() && remaining[0] == '/') remaining = remaining.substr(1);
            if (!remaining.empty() && remaining.back() == '/') remaining.pop_back();

            bool found = true;
            if (!remaining.empty()) {
                size_t pos = 0;
                while ((pos = remaining.find('/')) != std::string::npos) {
                    std::string part = remaining.substr(0, pos);
                    remaining = remaining.substr(pos + 1);
                    if (part.empty() || part == ".") continue;
                    
                    if (curr->children.find(part) == curr->children.end()) { found = false; break; }
                    curr = curr->children[part];
                }
                if (found && !remaining.empty()) {
                    if (curr->children.find(remaining) == curr->children.end()) { found = false; }
                    else { curr = curr->children[remaining]; }
                }
            }

            if (!found) {
                res.status = 404;
                res.set_content(generate_error_page(404, "Not Found", "Resource not found in CFUP archive"), "text/html");
                return;
            }

            if (curr->is_dir) {
                if (url_path.back() != '/') { res.set_redirect(url_path + "/"); return; }
                
                std::vector<VDirEntry> entries;
                for (auto& [name, child] : curr->children) {
                    VDirEntry e;
                    e.name = name;
                    e.is_dir = child->is_dir;
                    e.size = child->size;
                    e.time_val = g_cfup_time_val;
                    e.time_str = g_cfup_time_str;
                    entries.push_back(e);
                }
                serve_directory_generic(entries, url_path, query_params, res);
                return;
            } else {
                // File streaming directly from CFUP
                uintmax_t file_size = curr->size;
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
                        } catch (...) { }
                    }
                }

                uintmax_t content_length = is_range ? (end - start + 1) : file_size;
                auto file_ptr = std::make_shared<std::ifstream>(g_cfup_file_path, std::ios::binary);
                
                if (!file_ptr->is_open()) {
                    res.status = 500;
                    res.set_content("Failed to open underlying CFUP file", "text/plain");
                    return;
                }

                if (is_range) {
                    res.status = 206;
                    std::string content_range = "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(file_size);
                    res.set_header("Content-Range", content_range.c_str());
                } else {
                    res.status = 200;
                    res.set_header("Accept-Ranges", "bytes");
                }

                res.set_content_provider(
                    content_length,
                    "application/octet-stream",
                    [file_ptr, node_offset = curr->offset, start](size_t offset, size_t length, httplib::DataSink &sink) {
                        uintmax_t file_pos = node_offset + start + offset;

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
                return;
            }
        }


        // --- NORMAL FILESYSTEM HANDLING ---
        std::string fs_path = resolve_safe_path(url_path);

        if (fs_path.empty()) {
            res.status = 403;
            res.set_content(generate_error_page(403, "Forbidden", "Access to this path is not allowed"), "text/html");
            return;
        }

        fs::path p = fs::path(fs_path);

        std::error_code exists_ec;
        bool exists = fs::exists(p, exists_ec);
        
        if (exists_ec || !exists) {
            res.status = 404;
            res.set_content(generate_error_page(404, "Not Found", "The requested resource could not be found"), "text/html");
            return;
        }

        // Handle symlinks
        if (!g_config.follow_symlinks) {
            std::error_code ec;
            fs::path canonical_path = fs::canonical(p, ec);

            if (!ec) {
                auto normalize_path_str = [](const std::string& path) -> std::string {
                    std::string res = path;
                    const char sep = static_cast<char>(fs::path::preferred_separator);
                    while (!res.empty() && res.back() == sep) res.pop_back();
                    return res;
                };

                std::string req_str = normalize_path_str(p.string());
                std::string real_str = normalize_path_str(canonical_path.string());

                if (req_str != real_str) {
                    res.status = 403;
                    res.set_content(generate_error_page(403, "Forbidden", "Symbolic links are not followed"), "text/html");
                    return;
                }
            } else {
                if (fs::is_symlink(p, ec) && !ec) {
                    res.status = 403;
                    res.set_content(generate_error_page(403, "Forbidden", "Symbolic links are not followed"), "text/html");
                    return;
                }
            }
        }

        if (fs::is_directory(p)) {
            if (url_path.back() != '/') {
                res.set_redirect(url_path + "/");
                return;
            }
            
            std::vector<VDirEntry> entries;
            std::error_code dir_ec;
            
            for (auto& entry : fs::directory_iterator(p, dir_ec)) {
                if (dir_ec) continue;
                
                VDirEntry ve;
                ve.name = entry.path().filename().string();
                ve.is_dir = entry.is_directory();
                
                std::error_code ec_size;
                ve.size = ve.is_dir ? 0 : fs::file_size(entry.path(), ec_size);
                
                std::error_code ec_time;
                ve.time_val = fs::last_write_time(entry.path(), ec_time);
                ve.time_str = ec_time ? "-" : time_to_string(ve.time_val);
                
                entries.push_back(ve);
            }
            
            serve_directory_generic(entries, url_path, query_params, res);
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
                    } catch (...) { }
                }
            }

            uintmax_t content_length = is_range ? (end - start + 1) : file_size;

            auto file_ptr = std::make_shared<std::ifstream>(p, std::ios::binary);
            if (!file_ptr->is_open()) {
                res.status = 500;
                res.set_content("Failed to open file", "text/plain");
                return;
            }

            if (is_range) {
                res.status = 206;
                std::string content_range = "bytes " + std::to_string(start) + "-" + std::to_string(end) + "/" + std::to_string(file_size);
                res.set_header("Content-Range", content_range.c_str());
            } else {
                res.status = 200;
                res.set_header("Accept-Ranges", "bytes");
            }

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
    std::cout << "Source:     " << real_root << (g_config.is_cfup_mode ? " [CFUP Archive]" : " [Directory]") << std::endl;
    std::cout << "Threads:    " << g_config.thread_pool_size << std::endl;
    std::cout << "Page Size:  " << g_config.page_size << " items" << std::endl;
    if (!g_config.is_cfup_mode) {
        std::cout << "Symlinks:   " << (g_config.follow_symlinks ? "Enabled" : "Disabled") << std::endl;
    }
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