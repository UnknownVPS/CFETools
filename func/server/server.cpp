#include "server.h"
#include "httplib.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iomanip>
#include <vector>
#include <iostream>

namespace fs = std::filesystem;

namespace {
    std::string html_escape(const std::string& s) {
        std::string out;
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

    std::string resolve_safe_path(const std::string& root_path, const std::string& url_path) {
        // Use canonical to resolve absolute path and remove any trailing dots or symlinks
        std::error_code ec;
        fs::path base = fs::canonical(root_path, ec);
        if (ec) {
            // Fallback to absolute if canonical fails (e.g. directory doesn't exist)
            base = fs::absolute(root_path).lexically_normal();
        } else {
            base = base.lexically_normal();
        }

        std::string rel_url = url_path;
        if (!rel_url.empty() && rel_url[0] == '/')
            rel_url.erase(0, 1);

        fs::path combined = (base / fs::path(rel_url)).lexically_normal();

        // Safe prefix check using 4-argument mismatch to prevent reading past end of shorter path
        auto [mismatch_base, mismatch_combined] = std::mismatch(
            base.begin(), base.end(),
            combined.begin(), combined.end()
        );

        // If base is a prefix of combined, mismatch_base must reach base.end()
        if (mismatch_base != base.end())
            return "";

        return combined.string();
    }

    std::string format_size(uintmax_t bytes) {
        if (bytes == 0) return "0 B";
        const char* suffixes[] = {"B", "KB", "MB", "GB", "TB"};
        int s = 0;
        double count = bytes;
        while (count >= 1024 && s < 4) {
            s++;
            count /= 1024;
        }
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(2) << count << " " << suffixes[s];
        return ss.str();
    }

    void serve_directory(const fs::path& dir_path, const std::string& url_path, httplib::Response& res) {
        std::string html = R"===(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>File Server</title>
</head>
<body style="font-family: monospace; max-width: 900px; margin: 20px auto;">

    <h1>Index of <span id="currentPathDisplay">{{PATH_DISPLAY}}</span></h1>
    
    <div style="margin-bottom: 20px;">
        {{UP_BUTTON}}
        <button onclick="window.location.reload()">Refresh</button>
    </div>

    <table border="1" cellpadding="10" cellspacing="0" style="width: 100%; border-collapse: collapse;">
        <thead style="background-color: #efefef;">
            <tr>
                <th style="text-align: left;">Name</th>
                <th style="width: 100px;">Type</th>
                <th style="width: 100px;">Size</th>
                <th style="width: 100px;">Action</th>
            </tr>
        </thead>
        <tbody id="fileList">
            {{FILE_ROWS}}
        </tbody>
    </table>
</body>
</html>
)===";

        std::vector<fs::directory_entry> entries;
        for (auto& entry : fs::directory_iterator(dir_path)) {
            entries.push_back(entry);
        }

        std::sort(entries.begin(), entries.end(), [](const fs::directory_entry& a, const fs::directory_entry& b) {
            bool a_is_dir = a.is_directory();
            bool b_is_dir = b.is_directory();
            if (a_is_dir == b_is_dir) {
                return a.path().filename().string() < b.path().filename().string();
            }
            return a_is_dir;
        });

        std::ostringstream rows;
        
        if (entries.empty()) {
            rows << "<tr><td colspan='4' style='text-align:center; color:gray;'>Empty Directory</td></tr>";
        } else {
            for (auto& entry : entries) {
                std::string name = html_escape(entry.path().filename().string());
                std::string link = url_path + name; 
                
                bool isDir = entry.is_directory();
                if (isDir) link += "/";

                std::string sizeStr = isDir ? "-" : format_size(entry.file_size());

                rows << "<tr>";
                if (isDir) {
                    rows << "<td><a href=\"" << link << "\"><b>" << name << "/</b></a></td>";
                } else {
                    rows << "<td><a href=\"" << link << "\">" << name << "</a></td>";
                }
                rows << "<td>" << (isDir ? "DIR" : "FILE") << "</td>";
                rows << "<td>" << sizeStr << "</td>";
                if (isDir) {
                    rows << "<td><button onclick=\"window.location.href='" << link << "'\">Open</button></td>";
                } else {
                    rows << "<td><a href=\"" << link << "\" download>Download</a></td>";
                }
                rows << "</tr>";
            }
        }

        std::string upBtnHtml;
        if (url_path == "/") {
            upBtnHtml = "<button disabled>.. (Parent Directory)</button>";
        } else {
            std::string parentPath = url_path;
            if (!parentPath.empty() && parentPath.back() == '/') parentPath.pop_back();
            size_t lastSlash = parentPath.find_last_of('/');
            
            if (lastSlash != std::string::npos) {
                parentPath = parentPath.substr(0, lastSlash + 1);
            } else {
                parentPath = "/"; 
            }
            
            upBtnHtml = "<button onclick=\"window.location.href='" + parentPath + "'\">.. (Parent Directory)</button>";
        }

        std::string pathDisplay = (url_path == "/") ? "/" : url_path;
        
        auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
            if (from.empty()) return;
            size_t start_pos = 0;
            while ((start_pos = str.find(from, start_pos)) != std::string::npos) {
                str.replace(start_pos, from.length(), to);
                start_pos += to.length();
            }
        };

        replace_all(html, "{{PATH_DISPLAY}}", pathDisplay);
        replace_all(html, "{{UP_BUTTON}}", upBtnHtml);
        replace_all(html, "{{FILE_ROWS}}", rows.str());

        res.set_content(html, "text/html");
    }
}

int start_server(const std::string& root_path, int port) {
    httplib::Server svr;

    if (!fs::exists(root_path) || !fs::is_directory(root_path)) {
        std::cerr << "Error: Directory does not exist: " << root_path << std::endl;
        return 1;
    }

    svr.Get(R"(/.*)", [&](const httplib::Request& req, httplib::Response& res) {
        std::string url_path = req.path;
        std::string fs_path = resolve_safe_path(root_path, url_path);

        if (fs_path.empty()) {
            res.status = 403;
            res.set_content("Forbidden", "text/plain");
            return;
        }

        fs::path p = fs::path(fs_path);

        if (!fs::exists(p)) {
            res.status = 404;
            res.set_content("Not Found", "text/plain");
            return;
        }

        if (fs::is_directory(p)) {
            if (url_path.back() != '/') {
                res.set_redirect(url_path + "/");
                return;
            }
            serve_directory(p, url_path, res);
        } else {
            // Serve file content using a provider for memory efficiency (streaming)
            size_t file_size = fs::file_size(p);
            
            res.set_content_provider(
                file_size,
                "application/octet-stream",
                [p](size_t offset, size_t length, httplib::DataSink &sink) {
                    std::ifstream file(p, std::ios::binary);
                    if (!file) return false;
                    
                    file.seekg(offset);
                    std::vector<char> buffer(std::min<size_t>(65536, length));
                    
                    while (length > 0) {
                        size_t to_read = std::min(buffer.size(), length);
                        file.read(buffer.data(), to_read);
                        size_t read = file.gcount();
                        
                        if (read == 0) break;
                        
                        if (!sink.write(buffer.data(), read)) return false;
                        
                        length -= read;
                    }
                    return true;
                }
            );
        }
    });

    std::cout << "Server running at http://0.0.0.0:" << port << "\n";
    std::cout << "Serving directory: " << fs::absolute(root_path) << "\n";
    
    if (!svr.listen("0.0.0.0", port)) {
        std::cerr << "Error: Failed to start server on port " << port << std::endl;
        return 1;
    }

    return 0;
}
