#pragma once
#include "command_base.h"
#include "../func/backup/backup.hpp"
#include "../utils/userinput/user_input.h"
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

extern std::string save_path;

class BackupCommand : public Command {
public:
    const char* name() const override {
        return "backup";
    }

    const char* description() const override {
        return "Folder backup and version management using .cfup snapshots";
    }

    const char* usage() const override {
        return
            "backup <subcommand> [args] [options]\n"
            "\n"
            "  Subcommands:\n"
            "    init    <folder> <store>              Init new backup store\n"
            "    snap    <folder> <store>              Create incremental snapshot\n"
            "    restore <store>  <version> <dest>     Restore a version\n"
            "    rebase  <store>  --to <version>       Promote version to baseline\n"
            "    list    <store>                       List all versions\n"
            "    drop    <store>  <version>            Remove a version\n"
            "    prune   <store>  --keep <n>           Retain only N most recent\n"
            "    status  <store>                       Show rebase advisory\n"
            "\n"
            "  Version naming (init / snap):\n"
            "    --ver <id>   Use explicit version id (e.g. --ver v1)\n"
            "    --vauto      Auto-generate dated id  (e.g. v_20260321_143022)\n"
            "    (neither)    Interactive prompt\n";
    }

    int minArgs() const override {
        return 1;
    }

    int run(CommandContext& ctx) override {
        if (ctx.args.empty()) {
            Logger::Log(LOG_ERROR, "No subcommand specified.");
            Logger::Log(LOG_INFO, usage());
            return 1;
        }

        const std::string& sub = ctx.args[0];

        if      (sub == "init")    return run_init(ctx);
        else if (sub == "snap")    return run_snap(ctx);
        else if (sub == "restore") return run_restore(ctx);
        else if (sub == "rebase")  return run_rebase(ctx);
        else if (sub == "list")    return run_list(ctx);
        else if (sub == "drop")    return run_drop(ctx);
        else if (sub == "prune")   return run_prune(ctx);
        else if (sub == "status")  return run_status(ctx);
        else {
            Logger::Log(LOG_ERROR, "Unknown backup subcommand: " + sub);
            Logger::Log(LOG_INFO, usage());
            return 1;
        }
    }

private:

    // ------------------------------------------------------------------
    // Resolve store path: prepend save_path if not absolute and -sp not set
    // Matches behaviour of PackCommand, CompressCommand etc.
    // ------------------------------------------------------------------
    static std::string resolve_store(const std::string& raw) {
        std::filesystem::path p(raw);
        if (p.is_absolute()) return raw;
        return (std::filesystem::path(save_path) / p).string();
    }

    // ------------------------------------------------------------------
    // Auto-generate a dated version id: "v_20260321_143022"
    // ------------------------------------------------------------------
    static std::string make_auto_version_id() {
        auto t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        std::ostringstream oss;
        oss << "v_" << std::put_time(&tm, "%Y%m%d_%H%M%S");
        return oss.str();
    }

    // ------------------------------------------------------------------
    // Resolve version id from flags or interactive prompt.
    // Priority: --ver <id>  >  --vauto  >  interactive prompt
    // ------------------------------------------------------------------
    static std::string resolve_version_id(const CommandContext& ctx,
                                          const std::string& prompt_hint) {
        // 1. Explicit --ver / -ver flag
        std::string explicit_ver = get_flag(ctx, "ver", "ver", "");
        if (!explicit_ver.empty()) return explicit_ver;

        // 2. --vauto flag → dated string
        if (ctx.boolFlags.count("vauto")) return make_auto_version_id();

        // 3. Interactive prompt
        Input input;
        std::string id = input.ask(
            "Enter version id for this " + prompt_hint +
            " (or leave blank to auto-generate): ");

        if (id.empty()) return make_auto_version_id();
        return id;
    }

    // ------------------------------------------------------------------
    // backup init <folder> <store> [--ver <id> | --vauto] [--label <text>]
    // ------------------------------------------------------------------
    int run_init(CommandContext& ctx) {
        if (ctx.args.size() < 3) {
            Logger::Log(LOG_ERROR, "Usage: backup init <folder> <store> [--ver <id>|--vauto]");
            return 1;
        }

        const std::string& folder = ctx.args[1];
        std::string store  = resolve_store(ctx.args[2]);
        std::string label  = get_flag(ctx, "label", "l", "initial backup");
        std::string ver_id = resolve_version_id(ctx, "baseline");

        Logger::Log(LOG_INFO, "Version id: " + ver_id);
        return backup_init(folder, store, ver_id, label) ? 0 : 1;
    }

    // ------------------------------------------------------------------
    // backup snap <folder> <store> [--ver <id> | --vauto] [--label <text>]
    // ------------------------------------------------------------------
    int run_snap(CommandContext& ctx) {
        if (ctx.args.size() < 3) {
            Logger::Log(LOG_ERROR, "Usage: backup snap <folder> <store> [--ver <id>|--vauto]");
            return 1;
        }

        const std::string& folder = ctx.args[1];
        std::string store  = resolve_store(ctx.args[2]);
        std::string label  = get_flag(ctx, "label", "l", "");
        std::string ver_id = resolve_version_id(ctx, "snapshot");

        Logger::Log(LOG_INFO, "Version id: " + ver_id);
        return backup_snap(folder, store, ver_id, label) ? 0 : 1;
    }

    // ------------------------------------------------------------------
    // backup restore <store> <version> <dest>
    // dest is resolved against save_path if relative
    // ------------------------------------------------------------------
    int run_restore(CommandContext& ctx) {
        if (ctx.args.size() < 4) {
            Logger::Log(LOG_ERROR, "Usage: backup restore <store> <version> <dest>");
            return 1;
        }

        std::string store   = resolve_store(ctx.args[1]);
        const std::string& version = ctx.args[2];
        const std::string& dest    = ctx.args[3];

        std::filesystem::path dest_path =
            std::filesystem::path(dest).is_absolute()
            ? std::filesystem::path(dest)
            : std::filesystem::path(save_path) / dest;

        return backup_restore(store, version, dest_path.string()) ? 0 : 1;
    }

    // ------------------------------------------------------------------
    // backup rebase <store> --to <version>
    // ------------------------------------------------------------------
    int run_rebase(CommandContext& ctx) {
        if (ctx.args.size() < 2) {
            Logger::Log(LOG_ERROR, "Usage: backup rebase <store> --to <version>");
            return 1;
        }

        std::string store      = resolve_store(ctx.args[1]);
        std::string to_version = get_flag(ctx, "to", "to", "");

        if (to_version.empty()) {
            if (ctx.args.size() >= 3) {
                to_version = ctx.args[2];
            } else {
                Logger::Log(LOG_ERROR, "Specify target version: --to <version_id>");
                return 1;
            }
        }

        return backup_rebase(store, to_version) ? 0 : 1;
    }

    // ------------------------------------------------------------------
    // backup list <store>
    // ------------------------------------------------------------------
    int run_list(CommandContext& ctx) {
        if (ctx.args.size() < 2) {
            Logger::Log(LOG_ERROR, "Usage: backup list <store>");
            return 1;
        }
        return backup_list(resolve_store(ctx.args[1])) ? 0 : 1;
    }

    // ------------------------------------------------------------------
    // backup drop <store> <version>
    // ------------------------------------------------------------------
    int run_drop(CommandContext& ctx) {
        if (ctx.args.size() < 3) {
            Logger::Log(LOG_ERROR, "Usage: backup drop <store> <version>");
            return 1;
        }
        return backup_drop(resolve_store(ctx.args[1]), ctx.args[2]) ? 0 : 1;
    }

    // ------------------------------------------------------------------
    // backup prune <store> --keep <n>
    // ------------------------------------------------------------------
    int run_prune(CommandContext& ctx) {
        if (ctx.args.size() < 2) {
            Logger::Log(LOG_ERROR, "Usage: backup prune <store> --keep <n>");
            return 1;
        }

        std::string store = resolve_store(ctx.args[1]);

        int keep = 5;
        std::string keep_str = get_flag(ctx, "keep", "k", "");
        if (!keep_str.empty()) {
            try { keep = std::stoi(keep_str); }
            catch (...) {
                Logger::Log(LOG_ERROR, "Invalid --keep value: " + keep_str);
                return 1;
            }
        } else if (ctx.args.size() >= 3) {
            try { keep = std::stoi(ctx.args[2]); }
            catch (...) {
                Logger::Log(LOG_ERROR, "Invalid keep count: " + ctx.args[2]);
                return 1;
            }
        }

        if (keep < 1) {
            Logger::Log(LOG_ERROR, "--keep must be >= 1");
            return 1;
        }

        return backup_prune(store, keep) ? 0 : 1;
    }

    // ------------------------------------------------------------------
    // backup status <store>
    // ------------------------------------------------------------------
    int run_status(CommandContext& ctx) {
        if (ctx.args.size() < 2) {
            Logger::Log(LOG_ERROR, "Usage: backup status <store>");
            return 1;
        }

        std::string store = resolve_store(ctx.args[1]);

        BackupManifest manifest;
        if (!read_manifest(store, manifest)) return 1;

        Logger::Log(LOG_INFO, "Store:            " + store);
        Logger::Log(LOG_INFO, "Baseline:         " + manifest.baseline_id);
        Logger::Log(LOG_INFO, "Total versions:   " + std::to_string(manifest.versions.size()));
        Logger::Log(LOG_INFO, "Rebase threshold: " +
                              std::to_string((int)(manifest.rebase_threshold * 100)) + "%");
        Logger::Log(LOG_INFO, "Anchor interval:  " +
                              std::to_string(manifest.anchor_interval));

        uint64_t total_patch = 0;
        uint64_t max_patch   = 0;
        for (const auto& v : manifest.versions) {
            if (v.relative_to_baseline != "is_baseline") {
                total_patch += v.patch_size;
                if (v.patch_size > max_patch) max_patch = v.patch_size;
            }
        }
        Logger::Log(LOG_INFO, "Total patch data: " +
                              std::to_string(total_patch / 1024) + " KB");
        Logger::Log(LOG_INFO, "Largest patch:    " +
                              std::to_string(max_patch / 1024) + " KB");

        RebaseMetrics metrics = backup_rebase_metrics(store);
        Logger::Log(LOG_INFO, "");
        if (metrics.should_rebase) {
            Logger::Log(LOG_WARNING, "Rebase recommended: " + metrics.reason);
            Logger::Log(LOG_WARNING, "Suggested baseline: " + metrics.suggested_baseline_id);
            Logger::Log(LOG_INFO,    "Run: cfx backup rebase " + store +
                                     " --to " + metrics.suggested_baseline_id);
        } else {
            Logger::Log(LOG_INFO, "Store health: OK — no rebase needed.");
        }

        return 0;
    }

    // ------------------------------------------------------------------
    // Helper: read a named flag with fallback to short form, then default
    // ------------------------------------------------------------------
    static std::string get_flag(const CommandContext& ctx,
                                 const std::string& longname,
                                 const std::string& shortname,
                                 const std::string& fallback) {
        if (ctx.flags.count(longname))  return ctx.flags.at(longname);
        if (ctx.flags.count(shortname)) return ctx.flags.at(shortname);
        return fallback;
    }
};

COMMAND(BackupCommand)