#include "dir_module.h"
#include "server_core.h"
#include "httplib.h"
#include "../folder_packer/folder_packer.h"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <array>
#include <vector>
#include <unordered_map>

namespace fs = std::filesystem;
namespace {
    struct VDirEntry {
        std::string name;
        bool        is_dir = false;
        uintmax_t   size  = 0;
        fs::file_time_type time_val{};
        std::string time_str;
    };

    std::string generate_breadcrumbs(const std::string& url_path) {
        std::ostringstream html;
        html << "<div style='margin-bottom: 10px;'><a href='/'>[Root]</a>";
        if (url_path != "/") {
            std::string accumulated, remaining = url_path;
            if (!remaining.empty() && remaining[0]=='/') remaining = remaining.substr(1);
            if (!remaining.empty() && remaining.back()=='/')  remaining.pop_back();
            size_t pos = 0;
            while ((pos = remaining.find('/')) != std::string::npos) {
                std::string part = remaining.substr(0,pos);
                accumulated += "/" + part;
                html << " / <a href='" << accumulated << "/'>[" << ServerCore::html_escape(part) << "]</a>";
                remaining = remaining.substr(pos+1);
            }
            if (!remaining.empty()) html << " / <b>[" << ServerCore::html_escape(remaining) << "]</b>";
        }
        html << "</div>";
        return html.str();
    }

    std::string generate_error_page(int status, const std::string& msg, const std::string& details="") {
        std::ostringstream html;
        html << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"UTF-8\">\n<title>Error " << status << "</title>\n"
             << "</head>\n<body style=\"font-family: monospace; margin: 20px;\">\n<h1>Error " << status << ": " << ServerCore::html_escape(msg) << "</h1>\n";
        if (!details.empty()) html << "<p><i>" << ServerCore::html_escape(details) << "</i></p>\n";
        html << "<hr>\n<p><a href=\"/\">[Back to Root]</a></p>\n</body>\n</html>\n";
        return html.str();
    }

    std::string resolve_safe_path(const std::string& root, const std::string& url_path) {
        std::string decoded = ServerCore::url_decode(url_path);
        if (!decoded.empty() && decoded[0]=='/') decoded.erase(0,1);
        fs::path combined = (fs::path(root) / fs::path(decoded)).lexically_normal();
        std::string combined_str = combined.string();
        std::string root_str     = fs::path(root).string();
        const char sep = static_cast<char>(fs::path::preferred_separator);
        if (!root_str.empty() && root_str.back()!=sep) root_str += sep;
        if (!combined_str.empty() && combined_str.back()!=sep && fs::is_directory(combined)) combined_str += sep;
        if (combined_str.size() < root_str.size() || combined_str.substr(0, root_str.size()) != root_str) {
            if (combined_str + sep != root_str) return "";
        }
        return combined.string();
    }

    void serve_directory(const std::vector<VDirEntry>& raw_entries, const std::string& url_path,
                         const std::unordered_map<std::string,std::string>& query_params, size_t page_size, httplib::Response& res) {
        size_t page = 1; bool show_hidden = false;
        std::string sort_by="name", sort_order="asc";
        if (auto it=query_params.find("page"); it!=query_params.end()) try { page = std::max<size_t>(1, std::stoull(it->second)); } catch(...) {}
        if (auto it=query_params.find("hidden"); it!=query_params.end()) show_hidden = (it->second=="1" || it->second=="true");
        if (auto it=query_params.find("sort");  it!=query_params.end()) sort_by=it->second;
        if (auto it=query_params.find("order"); it!=query_params.end()) sort_order=it->second;
        const bool desc = (sort_order == "desc");

        std::vector<VDirEntry> entries;
        for (auto& e : raw_entries) {
            if (!show_hidden && !e.name.empty() && e.name[0]=='.') continue;
            entries.push_back(e);
        }

        std::sort(entries.begin(), entries.end(), [&](const VDirEntry& a, const VDirEntry& b){
            if (a.is_dir != b.is_dir) return a.is_dir;
            if (sort_by=="name" || sort_by=="type") return desc ? a.name>b.name : a.name<b.name;
            if (sort_by=="size") { if (a.size==b.size) return a.name<b.name; return desc ? a.size>b.size : a.size<b.size; }
            if (sort_by=="time") { if (a.time_val==b.time_val) return a.name<b.name; return desc ? a.time_val>b.time_val : a.time_val<b.time_val; }
            return a.name < b.name;
        });

        size_t total = entries.size(), pages = (total + page_size - 1) / page_size;
        size_t start = (page-1) * page_size, end = std::min(start + page_size, total);

        auto hdr = [&](const std::string& col, const std::string& lbl) -> std::string {
            std::string next = "asc", arrow;
            if (sort_by==col) { if (desc) { next="asc"; arrow=" &#8595;"; } else { next="desc"; arrow=" &#8593;"; } }
            return "<a href=\"javascript:void(0)\" onclick=\"setSort('"+col+"','"+next+"')\">" + lbl + arrow + "</a>";
        };

        std::ostringstream html;
        html << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"UTF-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
             << "<title>Index of " << ServerCore::html_escape(url_path) << "</title>\n<style>\nbody { font-family: monospace; margin: 20px; }\n"
             << "a { color: #0000EE; text-decoration: none; }\na:hover { text-decoration: underline; }\nth a { color: #000; font-weight: bold; display: block; }\n"
             << "table { border-collapse: collapse; width: 100%; }\nth, td { border: 1px solid #ccc; padding: 5px; }\nth { background-color: #efefef; text-align: left; }\n"
             << ".btn { cursor: pointer; }\n</style>\n</head>\n<body>\n<h1>Index of " << ServerCore::html_escape(url_path) << "</h1>\n"
             << generate_breadcrumbs(url_path) << "<hr>\n<div style=\"margin-bottom: 15px;\">\n";

        if (url_path != "/") {
            std::string parent = url_path;
            if (!parent.empty() && parent.back()=='/') parent.pop_back();
            size_t ls = parent.find_last_of('/');
            parent = (ls!=std::string::npos) ? parent.substr(0,ls+1) : "/";
            html << "<button class=\"btn\" onclick=\"window.location.href='" << parent << "'\">[Parent Directory]</button>\n";
        }
        html << "<button class=\"btn\" onclick=\"window.location.reload()\">[Refresh]</button>\n"
             << "<button class=\"btn\" id=\"btnHidden\" onclick=\"toggleHidden()\">[Show Hidden]</button>\n</div>\n<hr>\n<table>\n<thead>\n<tr>\n"
             << "<th style=\"width: 40%;\">" << hdr("name","Name") << "</th>\n<th style=\"width: 80px;\">Type</th>\n"
             << "<th style=\"width: 120px;\">" << hdr("size","Size") << "</th>\n<th style=\"width: 160px;\">" << hdr("time","Modified") << "</th>\n"
             << "<th style=\"width: 200px;\">Action</th>\n</tr>\n</thead>\n<tbody>\n";

        if (entries.empty()) {
            html << "<tr><td colspan=\"5\" style=\"text-align:center;\">Empty Directory</td></tr>\n";
        } else {
            for (size_t i=start; i<end; ++i) {
                auto& e = entries[i];
                std::string safe = ServerCore::html_escape(e.name);
                std::string link = url_path + e.name;
                if (e.is_dir) link += "/";

                html << "<tr><td>" << (e.is_dir ? "[DIR]" : "[FILE]") << " ";
                if (e.is_dir) html << "<b><a href=\"" << link << "\">" << safe << "/</a></b>";
                else          html << "<a href=\"" << link << "\">" << safe << "</a>";
                html << "</td><td>" << (e.is_dir?"DIR":"FILE") << "</td><td>" << (e.is_dir ? "-" : ServerCore::format_size(e.size))
                     << "</td><td>" << e.time_str << "</td><td>";

                if (e.is_dir) {
                    html << "<button class=\"btn\" onclick=\"window.location.href='" << link << "'\">[Open]</button> ";
                    html << "<button class=\"btn\" onclick=\"window.location.href='" << link << "?as_cfup=1'\">[Download as CFUP]</button>";
                } else {
                    html << "<a href=\"" << link << "\" download>[Download]</a>";
                    bool is_cfup = e.name.size() >= 5 && (e.name.compare(e.name.size()-5, 5, ".cfup") == 0);
                    if (is_cfup) {
                        html << " <button class=\"btn\" onclick=\"window.location.href='/__cfup_open__" << link << "'\">[Open]</button>";
                    }
                }
                html << "</td></tr>\n";
            }
        }

        html << "</tbody>\n</table>\n";
        if (pages > 1) {
            html << "<hr>\n<div style=\"text-align: center; margin-top: 10px;\">\n";
            if (page>1) html << "<a href=\"javascript:void(0)\" onclick=\"goToPage(" << (page-1) << ")\">[&lt; Prev]</a> \n";
            else         html << "<span style=\"color: gray;\">[&lt; Prev]</span> \n";
            html << "Page " << page << " of " << pages << " (" << total << " items) \n";
            if (page<pages) html << "<a href=\"javascript:void(0)\" onclick=\"goToPage(" << (page+1) << ")\">[Next &gt;]</a>\n";
            else            html << "<span style=\"color: gray;\">[Next &gt;]</span>\n</div>\n";
        }
        html << "<script>\nfunction updateHiddenButton(){const u=new URL(window.location);const h=u.searchParams.get('hidden')==='1';const b=document.getElementById('btnHidden');if(b)b.innerText=h?\"[Hide Hidden]\":\"[Show Hidden]\";}\n"
             << "function toggleHidden(){const u=new URL(window.location);if(u.searchParams.get('hidden')==='1')u.searchParams.delete('hidden');else u.searchParams.set('hidden','1');u.searchParams.delete('page');window.location.href=u.toString();}\n"
             << "function setSort(c,o){const u=new URL(window.location);u.searchParams.set('sort',c);u.searchParams.set('order',o);u.searchParams.delete('page');window.location.href=u.toString();}\n"
             << "function goToPage(p){const u=new URL(window.location);u.searchParams.set('page',p);window.location.href=u.toString();}\nupdateHiddenButton();\n</script>\n</body>\n</html>\n";
        res.set_content(html.str(), "text/html");
    }

    void serve_file_range(const std::string& fs_path, const httplib::Request& req, httplib::Response& res) {
        std::error_code ec;
        uintmax_t file_size = fs::file_size(fs_path, ec);
        if (ec) { res.status=500; res.set_content("Could not determine file size","text/plain"); return; }

        uintmax_t start = 0, end = file_size ? file_size - 1 : 0;
        bool is_range = false;
        std::string range = req.get_header_value("Range");
        if (!range.empty() && range.find("bytes=")==0) {
            std::string spec = range.substr(6); size_t dash = spec.find('-');
            if (dash!=std::string::npos) {
                try {
                    std::string s = spec.substr(0,dash); std::string e = spec.substr(dash+1);
                    if (!s.empty()) start = std::stoull(s);
                    if (!e.empty()) end   = std::stoull(e); else end = file_size ? file_size-1 : 0;
                    if (start < file_size && end < file_size && start <= end) is_range = true;
                } catch (...) {}
            }
        }

        uintmax_t content_length = is_range ? (end-start+1) : file_size;
        auto fp = std::make_shared<std::ifstream>(fs_path, std::ios::binary);
        if (!fp->is_open()) { res.status=500; res.set_content("Failed to open file","text/plain"); return; }

        if (is_range) {
            res.status = 206;
            res.set_header("Content-Range", ("bytes "+std::to_string(start)+"-"+std::to_string(end)+"/"+std::to_string(file_size)).c_str());
        } else { res.status = 200; res.set_header("Accept-Ranges","bytes"); }

        res.set_content_provider(content_length, "application/octet-stream",
            [fp, start](size_t off, size_t len, httplib::DataSink& sink) -> bool {
                uintmax_t pos = start + off;
                fp->clear(); fp->seekg(pos);
                if (!fp->good()) return false;
                constexpr size_t kChunk = 64 * 1024;
                std::array<char,kChunk> buf;
                size_t to_read = std::min(len, kChunk);
                fp->read(buf.data(), to_read);
                size_t got = fp->gcount();
                if (got>0) sink.write(buf.data(), got);
                return true;
            });
    }

    // DIRECTLY uses pack_folder_stream, giving us perfect backpressure handling
    void serve_dir_as_cfup(const std::string& dir_path, bool follow_symlinks, httplib::Response& res) {
        res.status = 200;
        res.set_header("Content-Type", "application/octet-stream");
        // Manually strip trailing slashes so filename() works correctly across all OS/compilers
        std::string clean_path = dir_path;
        while (!clean_path.empty() && (clean_path.back() == '/' || clean_path.back() == '\\')) {
            clean_path.pop_back();
        }
        std::string filename = fs::path(clean_path).filename().string();
        if (filename.empty() || filename == "." || filename == "..") filename = "root";
        res.set_header("Content-Disposition", ("attachment; filename=\"" + filename + ".cfup\"").c_str());

        res.set_chunked_content_provider("application/octet-stream",
            [dir_path, follow_symlinks](size_t offset, httplib::DataSink& sink) -> bool {
                if (offset > 0) return false; // Only run once
                WriteFn writer = [&sink](const void* data, size_t len) -> bool {
                    return sink.write(static_cast<const char*>(data), len);
                };
                bool ok = pack_folder_stream(dir_path, writer, follow_symlinks);
                if (ok) sink.done();
                return ok;
            });
    }
}

DirModule::DirModule(std::string root, bool fs_, size_t ps) : root_(std::move(root)), follow_symlinks_(fs_), page_size_(ps) {}

bool DirModule::handle(ModuleContext& ctx) {
    if (ctx.decoded_path.rfind("/__cfup_open__", 0) == 0) return false; // Belongs to CFUP module

    std::string fs_path = resolve_safe_path(root_, ctx.url_path);
    if (fs_path.empty()) {
        ctx.res.status = 403;
        ctx.res.set_content(generate_error_page(403,"Forbidden","Access to this path is not allowed"),"text/html");
        return true;
    }

    fs::path p(fs_path);
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        ctx.res.status = 404;
        ctx.res.set_content(generate_error_page(404,"Not Found","The requested resource could not be found"),"text/html");
        return true;
    }

    // Symlink logic unchanged
    if (!follow_symlinks_) {
        fs::path canon = fs::canonical(p, ec);
        if (!ec) {
            auto norm = [](const std::string& s) {
                std::string r = s; const char sep = static_cast<char>(fs::path::preferred_separator);
                while (!r.empty() && r.back()==sep) r.pop_back(); return r;
            };
            if (norm(p.string()) != norm(canon.string())) {
                ctx.res.status = 403; ctx.res.set_content(generate_error_page(403,"Forbidden","Symbolic links are not followed"),"text/html"); return true;
            }
        } else if (fs::is_symlink(p, ec) && !ec) {
            ctx.res.status = 403; ctx.res.set_content(generate_error_page(403,"Forbidden","Symbolic links are not followed"),"text/html"); return true;
        }
    }

    if (fs::is_directory(p)) {
        if (ctx.url_path.back() != '/') { ctx.res.set_redirect(ctx.url_path + "/"); return true; }
        if (auto it = ctx.query.find("as_cfup"); it != ctx.query.end() && (it->second=="1" || it->second=="true")) {
            serve_dir_as_cfup(fs_path, follow_symlinks_, ctx.res);
            return true;
        }
        std::vector<VDirEntry> entries; std::error_code dir_ec;
        for (auto& e : fs::directory_iterator(p, dir_ec)) {
            if (dir_ec) { dir_ec.clear(); continue; }
            VDirEntry ve; ve.name  = e.path().filename().string(); ve.is_dir = e.is_directory();
            std::error_code ec2; ve.size = ve.is_dir ? 0 : fs::file_size(e.path(), ec2);
            std::error_code ec3; ve.time_val = fs::last_write_time(e.path(), ec3); ve.time_str = ec3 ? "-" : ServerCore::time_to_string(ve.time_val);
            entries.push_back(ve);
        }
        serve_directory(entries, ctx.url_path, ctx.query, page_size_, ctx.res);
        return true;
    }

    serve_file_range(fs_path, ctx.req, ctx.res);
    return true;
}