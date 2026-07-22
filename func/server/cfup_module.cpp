#include "cfup_module.h"
#include "server_core.h"
#include "httplib.h"
#include "../folder_packer/folder_packer.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <array>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace fs = std::filesystem;
namespace {
    struct VDirEntry {
        std::string name; bool is_dir = false; uintmax_t size  = 0;
        fs::file_time_type time_val{}; std::string time_str;
    };

    std::string generate_breadcrumbs(const std::string& display_path, const std::string& virtual_base_url) {
        std::ostringstream html;
        html << "<div style='margin-bottom: 10px;'><a href='/'>[Root]</a>";
        if (display_path != "/" && display_path != virtual_base_url + "/") {
            std::string remaining = display_path;
            if (!remaining.empty() && remaining[0]=='/') remaining = remaining.substr(1);
            if (!remaining.empty() && remaining.back()=='/')  remaining.pop_back();
            
            // The first component is the archive name, which maps to virtual_base_url
            size_t pos = remaining.find('/');
            std::string arch_name = (pos == std::string::npos) ? remaining : remaining.substr(0, pos);
            html << " / <a href='" << virtual_base_url << "/'>[" << ServerCore::html_escape(arch_name) << "]</a>";
            
            if (pos != std::string::npos) {
                remaining = remaining.substr(pos + 1);
                std::string link_acc = virtual_base_url + "/";
                pos = 0;
                while ((pos = remaining.find('/')) != std::string::npos) {
                    std::string part = remaining.substr(0,pos);
                    link_acc += part + "/";
                    html << " / <a href='" << link_acc << "'>[" << ServerCore::html_escape(part) << "]</a>";
                    remaining = remaining.substr(pos+1);
                }
                if (!remaining.empty())
                    html << " / <b>[" << ServerCore::html_escape(remaining) << "]</b>";
            }
        }
        html << "</div>"; return html.str();
    }

    std::string generate_error_page(int status, const std::string& msg, const std::string& details="") {
        std::ostringstream html;
        html << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"UTF-8\">\n<title>Error " << status << "</title>\n"
             << "</head>\n<body style=\"font-family: monospace; margin: 20px;\">\n<h1>Error " << status << ": " << ServerCore::html_escape(msg) << "</h1>\n";
        if (!details.empty()) html << "<p><i>" << ServerCore::html_escape(details) << "</i></p>\n";
        html << "<hr>\n<p><a href=\"/\">[Back to Root]</a></p>\n</body>\n</html>\n"; return html.str();
    }

    void serve_directory(const std::vector<VDirEntry>& raw_entries, const std::string& url_path, const std::string& virtual_base_url,
                         const std::unordered_map<std::string,std::string>& query_params, size_t page_size, httplib::Response& res) {
        
        // Compute a clean display path that hides the internal /__cfup_open__ route
        std::string display_path = url_path;
        const std::string open_prefix = "/__cfup_open__";
        if (display_path.rfind(open_prefix, 0) == 0) {
            display_path = display_path.substr(open_prefix.size());
            if (display_path.empty()) display_path = "/";
        }

        size_t page = 1; bool show_hidden = false;
        std::string sort_by="name", sort_order="asc";
        if (auto it=query_params.find("page"); it!=query_params.end()) try { page = std::max<size_t>(1, std::stoull(it->second)); } catch(...) {}
        if (auto it=query_params.find("hidden"); it!=query_params.end()) show_hidden = (it->second=="1" || it->second=="true");
        if (auto it=query_params.find("sort");  it!=query_params.end()) sort_by=it->second;
        if (auto it=query_params.find("order"); it!=query_params.end()) sort_order=it->second;
        const bool desc = (sort_order=="desc");

        std::vector<VDirEntry> entries;
        for (auto& e : raw_entries) { if (!show_hidden && !e.name.empty() && e.name[0]=='.') continue; entries.push_back(e); }
        std::sort(entries.begin(), entries.end(), [&](const VDirEntry& a, const VDirEntry& b){
            if (a.is_dir!=b.is_dir) return a.is_dir;
            if (sort_by=="name"||sort_by=="type") return desc ? a.name>b.name : a.name<b.name;
            if (sort_by=="size")  { if (a.size==b.size) return a.name<b.name; return desc?a.size>b.size:a.size<b.size; }
            if (sort_by=="time")  { if (a.time_val==b.time_val) return a.name<b.name; return desc?a.time_val>b.time_val:a.time_val<b.time_val; }
            return a.name<b.name;
        });

        size_t total = entries.size(), pages = (total + page_size - 1) / page_size;
        size_t start = (page-1)*page_size, end = std::min(start+page_size, total);
        auto hdr = [&](const std::string& col, const std::string& lbl) -> std::string {
            std::string next="asc", arrow;
            if (sort_by==col) { if (desc){next="asc";arrow=" &#8595;";} else {next="desc";arrow=" &#8593;";} }
            return "<a href=\"javascript:void(0)\" onclick=\"setSort('"+col+"','"+next+"')\">"+lbl+arrow+"</a>";
        };

        std::ostringstream html;
        html << "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n<meta charset=\"UTF-8\">\n<meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
             << "<title>Index of " << ServerCore::html_escape(display_path) << "</title>\n<style>\nbody { font-family: monospace; margin: 20px; }\n"
             << "a { color: #0000EE; text-decoration: none; }\na:hover { text-decoration: underline; }\nth a { color: #000; font-weight: bold; display: block; }\n"
             << "table { border-collapse: collapse; width: 100%; }\nth, td { border: 1px solid #ccc; padding: 5px; }\nth { background-color: #efefef; text-align: left; }\n"
             << ".btn { cursor: pointer; }\n</style>\n</head>\n<body>\n<h1>Index of " << ServerCore::html_escape(display_path) << "</h1>\n"
             << generate_breadcrumbs(display_path, virtual_base_url) << "<hr>\n<div style=\"margin-bottom: 15px;\">\n";

        if (url_path != "/" && url_path != virtual_base_url + "/") {
            std::string parent = url_path;
            if (!parent.empty() && parent.back()=='/') parent.pop_back();
            size_t ls = parent.find_last_of('/');
            parent = (ls!=std::string::npos) ? parent.substr(0,ls+1) : "/";
            
            // If parent is the root of the virtual archive, go to the real directory instead
            if (parent == virtual_base_url + "/" || parent == virtual_base_url) {
                std::string real_path = virtual_base_url;
                const std::string open_prefix = "/__cfup_open__";
                if (real_path.rfind(open_prefix, 0) == 0) real_path = real_path.substr(open_prefix.size());
                if (real_path.empty()) real_path = "/";
                fs::path p = real_path;
                parent = p.parent_path().string();
                if (parent.empty()) parent = "/";
                if (parent.back() != '/') parent += "/";
            }
            html << "<button class=\"btn\" onclick=\"window.location.href='"<<parent<<"'\">[Parent Directory]</button>\n";
        }
        html << "<button class=\"btn\" onclick=\"window.location.reload()\">[Refresh]</button>\n"
             << "<button class=\"btn\" id=\"btnHidden\" onclick=\"toggleHidden()\">[Show Hidden]</button>\n</div>\n<hr>\n<table>\n<thead>\n<tr>\n"
             << "<th style=\"width: 40%;\">" << hdr("name","Name") << "</th>\n<th style=\"width: 80px;\">Type</th>\n"
             << "<th style=\"width: 120px;\">" << hdr("size","Size") << "</th>\n<th style=\"width: 160px;\">" << hdr("time","Modified") << "</th>\n"
             << "<th style=\"width: 100px;\">Action</th>\n</tr>\n</thead>\n<tbody>\n";

        if (entries.empty()) { html << "<tr><td colspan=\"5\" style=\"text-align:center;\">Empty Directory</td></tr>\n"; }
        else {
            for (size_t i=start; i<end; ++i) {
                auto& e = entries[i];
                std::string safe = ServerCore::html_escape(e.name);
                std::string link = url_path + e.name; if (e.is_dir) link += "/";
                html << "<tr><td>" << (e.is_dir ? "[DIR]" : "[FILE]") << " ";
                if (e.is_dir) html << "<b><a href=\""<<link<<"\">"<<safe<<"/</a></b>";
                else          html << "<a href=\""<<link<<"\">"<<safe<<"</a>";
                html << "</td><td>"<<(e.is_dir?"DIR":"FILE")<<"</td><td>"<<(e.is_dir?"-":ServerCore::format_size(e.size))<<"</td><td>"<<e.time_str<<"</td><td>";
                if (e.is_dir) html << "<button class=\"btn\" onclick=\"window.location.href='"<<link<<"'\">[Open]</button>";
                else          html << "<a href=\""<<link<<"\" download>[Download]</a>";
                html << "</td></tr>\n";
            }
        }
        html << "</tbody>\n</table>\n";
        if (pages>1) {
            html << "<hr>\n<div style=\"text-align: center; margin-top: 10px;\">\n";
            if (page>1) html << "<a href=\"javascript:void(0)\" onclick=\"goToPage("<<(page-1)<<")\">[&lt; Prev]</a> \n";
            else        html << "<span style=\"color: gray;\">[&lt; Prev]</span> \n";
            html << "Page " << page << " of " << pages << " (" << total << " items) \n";
            if (page<pages) html << "<a href=\"javascript:void(0)\" onclick=\"goToPage("<<(page+1)<<")\">[Next &gt;]</a>\n";
            else            html << "<span style=\"color: gray;\">[Next &gt;]</span>\n</div>\n";
        }
        html << "<script>\nfunction updateHiddenButton(){const u=new URL(window.location);const h=u.searchParams.get('hidden')==='1';const b=document.getElementById('btnHidden');if(b)b.innerText=h?\"[Hide Hidden]\":\"[Show Hidden]\";}\n"
             << "function toggleHidden(){const u=new URL(window.location);if(u.searchParams.get('hidden')==='1')u.searchParams.delete('hidden');else u.searchParams.set('hidden','1');u.searchParams.delete('page');window.location.href=u.toString();}\n"
             << "function setSort(c,o){const u=new URL(window.location);u.searchParams.set('sort',c);u.searchParams.set('order',o);u.searchParams.delete('page');window.location.href=u.toString();}\n"
             << "function goToPage(p){const u=new URL(window.location);u.searchParams.set('page',p);window.location.href=u.toString();}\nupdateHiddenButton();\n</script>\n</body>\n</html>\n";
        res.set_content(html.str(), "text/html");
    }

    const PackedFileEntry* find_entry(const std::vector<PackedFileEntry>& toc, const std::string& vpath) {
        for (auto& e : toc) if (e.path == vpath) return &e;
        return nullptr;
    }

    void serve_file_range(const std::string& archive_path, uint64_t file_offset, uint64_t file_size, const httplib::Request& req, httplib::Response& res) {
        uint64_t start = 0, end = file_size ? file_size-1 : 0;
        bool is_range = false;
        std::string range = req.get_header_value("Range");
        if (!range.empty() && range.find("bytes=")==0) {
            std::string spec = range.substr(6); size_t dash = spec.find('-');
            if (dash!=std::string::npos) {
                try {
                    std::string s = spec.substr(0,dash); std::string e = spec.substr(dash+1);
                    if (!s.empty()) start = std::stoull(s);
                    if (!e.empty()) end   = std::stoull(e); else end = file_size ? file_size-1 : 0;
                    if (start < file_size && end < file_size && start <= end) is_range=true;
                } catch (...) {}
            }
        }
        uint64_t content_length = is_range ? (end-start+1) : file_size;
        auto fp = std::make_shared<std::ifstream>(archive_path, std::ios::binary);
        if (!fp->is_open()) { res.status=500; res.set_content("Failed to open archive","text/plain"); return; }

        if (is_range) {
            res.status = 206;
            res.set_header("Content-Range", ("bytes "+std::to_string(start)+"-"+std::to_string(end)+"/"+std::to_string(file_size)).c_str());
        } else { res.status = 200; res.set_header("Accept-Ranges","bytes"); }

        res.set_content_provider(content_length, "application/octet-stream",
            [fp, file_offset, start](size_t off, size_t len, httplib::DataSink& sink) -> bool {
                uint64_t pos = file_offset + start + off;
                fp->clear(); fp->seekg(pos);
                if (!fp->good()) return false;
                constexpr size_t kChunk = 64*1024;
                std::array<char,kChunk> buf;
                size_t to_read = std::min(len, kChunk);
                fp->read(buf.data(), to_read);
                size_t got = (size_t)fp->gcount();
                if (got>0) sink.write(buf.data(), got);
                return true;
            });
    }
}

CfupModule::CfupModule(std::string mount_path, std::string fs_root, size_t ps)
    : mount_path_(std::move(mount_path)), fs_root_(std::move(fs_root)), page_size_(ps) {}

bool CfupModule::init(ServerCore&) {
    if (mount_path_.empty()) return true; // Open-route only mode
    if (!list_packed_files(mount_path_, entries_)) return false;
    archive_path_ = mount_path_;
    std::error_code ec;
    mtime_ = fs::last_write_time(mount_path_, ec);
    mtime_str_ = ec ? "-" : ServerCore::time_to_string(mtime_);
    return true;
}

bool CfupModule::handle(ModuleContext& ctx) {
    // Keep temp_entries alive across the entire function
    std::vector<PackedFileEntry> temp_entries;
    std::vector<PackedFileEntry>* entries = nullptr;
    std::string archive_path;
    fs::file_time_type mtime;
    std::string mtime_str;
    std::string virtual_base_url;

    if (!mount_path_.empty()) {
        // Handle primary mounted archive
        entries = &entries_;
        archive_path = archive_path_;
        mtime = mtime_;
        mtime_str = mtime_str_;
        virtual_base_url = "/"; // Root mount
    } else {
        // Open route /__cfup_open__
        const std::string open_prefix = "/__cfup_open__";
        if (ctx.url_path.rfind(open_prefix, 0) != 0) return false; // Not for us

        std::string rest = ctx.url_path.substr(open_prefix.size());
        fs::path p = rest;
        if (!p.is_absolute()) p = "/" / p;

        // Use fs_root_ to locate the actual file on disk
        fs::path current = fs_root_ / p.relative_path();
        current = current.lexically_normal();

        fs::path archive_fs_path;
        while (!current.empty()) {
            if (fs::exists(current) && fs::is_regular_file(current)) {
                archive_fs_path = current;
                break;
            }
            if (current == current.parent_path() || current == fs_root_) break;
            current = current.parent_path();
        }

        if (archive_fs_path.empty()) return false; // Let others handle
        
        archive_path = archive_fs_path.string();
        
        // Calculate the correct virtual_base_url based on requested URL
        std::string arch_name = archive_fs_path.filename().string();
        size_t pos = rest.find(arch_name);
        if (pos == std::string::npos) return false;
        
        virtual_base_url = open_prefix + rest.substr(0, pos + arch_name.size());
        
        if (!list_packed_files(archive_path, temp_entries)) {
            ctx.res.status = 500; ctx.res.set_content("Failed to read CFUP archive", "text/plain"); return true;
        }
        std::error_code ec;
        mtime = fs::last_write_time(archive_path, ec);
        mtime_str = ec ? "-" : ServerCore::time_to_string(mtime);

        entries = &temp_entries;
    }

    std::string rel_url = ctx.url_path.substr(virtual_base_url.size());
    if (rel_url.empty()) rel_url = "/";
    if (rel_url[0] != '/') rel_url = "/" + rel_url;

    if (rel_url.find("..") != std::string::npos) {
        ctx.res.status = 403; ctx.res.set_content(generate_error_page(403,"Forbidden","Invalid path traversal"),"text/html"); return true;
    }

    std::string vpath = rel_url;
    if (!vpath.empty() && vpath[0]=='/') vpath.erase(0,1);
    if (!vpath.empty() && vpath.back()=='/') vpath.pop_back();

    const PackedFileEntry* entry = nullptr;
    bool is_dir = false;

    if (vpath.empty()) { is_dir = true; }
    else {
        entry = find_entry(*entries, vpath);
        if (!entry) {
            std::string base = vpath + "/";
            for (auto& e : *entries) {
                if (e.path.rfind(base, 0) == 0) { is_dir = true; break; }
            }
        }
    }

    if (!entry && !is_dir) {
        ctx.res.status = 404; ctx.res.set_content(generate_error_page(404,"Not Found","Resource not found in CFUP archive"),"text/html"); return true;
    }

    if (is_dir) {
        if (ctx.url_path.back() != '/') { ctx.res.set_redirect(ctx.url_path + "/"); return true; }
        std::vector<VDirEntry> dir_entries;
        std::map<std::string, VDirEntry> seen;
        std::string base = vpath;
        if (!base.empty()) base += "/";
        for (auto& e : *entries) {
            if (e.path.size() < base.size() || e.path.compare(0, base.size(), base) != 0) continue;
            std::string rest = e.path.substr(base.size());
            size_t slash = rest.find('/');
            if (slash == std::string::npos) {
                VDirEntry v; v.name = rest; v.is_dir = false; v.size = e.size; seen[rest] = v;
            } else {
                std::string dir = rest.substr(0, slash);
                if (seen.find(dir) == seen.end()) { VDirEntry v; v.name = dir; v.is_dir = true; v.size = 0; seen[dir] = v; }
            }
        }
        for (auto& [_, v] : seen) {
            v.time_val = mtime; v.time_str = mtime_str;
            dir_entries.push_back(v);
        }
        serve_directory(dir_entries, ctx.url_path, virtual_base_url, ctx.query, page_size_, ctx.res);
    } else {
        serve_file_range(archive_path, entry->offset, entry->size, ctx.req, ctx.res);
    }
    return true;
}