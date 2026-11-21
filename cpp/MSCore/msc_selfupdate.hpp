// msc_selfupdate.hpp - MSCoreManager.exe updates itself from the latest MSCore release (manager only, never the DLL).
// Manifest keys (written by scripts\release.ps1): manager_version (version baked into the MSCoreManager.exe asset),
// manager_sha256, manager_size. The asset is MSCoreManager.exe of the release that holds the manifest. A manifest without
// those keys = no self-update ("manager update info missing"): reused-exe releases keep the old keys of the previous manifest,
// which is why the exe version is its own key and not the release version.
#pragma once
#include "msc_loader.hpp"

namespace mscl {

struct SelfUpdateInfo {
    std::string release_version;  // manifest version = release tag, where the asset is
    std::string version;          // manager_version
    std::string sha256;
    long long size = 0;
};

enum class SuSt { Newer, UpToDate, NoInfo, Error };

// Reads the manifest of repo. Newer = manager_version > own. Error = no network etc. (callers stay silent on it).
inline SuSt self_update_check(const std::string& repo, const std::string& own_version, SelfUpdateInfo& info, std::string& err) {
    std::string text;
    if (!http_get(manifest_url(repo), text, err, 1u << 20)) return SuSt::Error;
    KV kv = parse_kv(text);
    info = SelfUpdateInfo{};
    info.release_version = kv["version"];
    info.version = kv["manager_version"];
    info.sha256 = lower(kv["manager_sha256"]);
    info.size = std::strtoll(kv["manager_size"].c_str(), nullptr, 10);
    int v[3];
    if (info.version.empty() || info.sha256.size() != 64 || info.size <= 0 || !safe_name(info.release_version) || !ver_parse(info.version, v)) {
        err = "manager update info missing";
        return SuSt::NoInfo;
    }
    return ver_cmp(info.version, own_version) > 0 ? SuSt::Newer : SuSt::UpToDate;
}

// Downloads the asset next to exe, verifies size, sha256 and the MZ header, then swaps: exe -> exe.old (a running exe can be
// renamed), staged -> exe. Any failure leaves exe exactly as it was. The new instance deletes exe.old (self_update_cleanup).
inline bool self_update_apply(const fs::path& exe, const std::string& repo, const SelfUpdateInfo& info, std::string& err) {
    std::error_code ec;
    fs::path staged = exe, old = exe;
    staged += L".new";
    old += L".old";
    struct Drop {
        fs::path f;
        bool keep = false;
        ~Drop() {
            std::error_code e;
            if (!keep) fs::remove(f, e);
        }
    } drop{staged};
    fs::remove(staged, ec);
    if (!http_download(zip_url(repo, info.release_version, "MSCoreManager.exe"), staged, err)) return false;
    std::string hex;
    if (!sha256_file(staged, hex) || lower(hex) != info.sha256) {
        err = "manager hash mismatch";
        return false;
    }
    if (static_cast<long long>(fs::file_size(staged, ec)) != info.size) {
        err = "manager size mismatch";
        return false;
    }
    std::string head = read_text(staged).substr(0, 2);
    if (head != "MZ") {
        err = "manager download is not an exe";
        return false;
    }
    fs::remove(old, ec);
    fs::rename(exe, old, ec);
    if (ec) {
        err = "cannot rename the running exe: " + ec.message();
        return false;
    }
    fs::rename(staged, exe, ec);
    if (ec) {
        std::error_code ec2;
        fs::rename(old, exe, ec2);  // roll back
        err = "cannot move the new exe into place: " + ec.message();
        return false;
    }
    drop.keep = true;  // staged was moved
    return true;
}

inline void self_update_cleanup(const fs::path& exe) {
    std::error_code ec;
    fs::path old = exe;
    old += L".old";
    fs::remove(old, ec);
}

}  // namespace mscl
