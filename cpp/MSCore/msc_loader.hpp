// msc_loader.hpp - MSCore loader core. Header-only, C++17, Win32 (WinHTTP, BCrypt, tar.exe).
// Shared by the DLL and the manager exe. No UE4SS dependency.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace mscl {

namespace fs = std::filesystem;
using KV = std::map<std::string, std::string>;

// Tests and exe --source only. DLL never sets these.
inline bool g_allow_local = false;
inline std::string g_local_root;  // with g_allow_local: manifest.txt + zips are read from here

// ---------- text / utf8 ----------

inline std::string to_utf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

inline std::wstring to_wide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
    return w;
}

inline std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

inline std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

// key=value, '#' or ';' comments, trimmed. Lines without '=' are ignored.
inline KV parse_kv(const std::string& text) {
    KV kv;
    std::istringstream in(text.compare(0, 3, "\xEF\xBB\xBF") == 0 ? text.substr(3) : text);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        kv[trim(line.substr(0, eq))] = trim(line.substr(eq + 1));
    }
    return kv;
}

// Missing/unreadable file -> "". Strips UTF-8 BOM.
inline std::string read_text(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return "";
    std::string s((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (s.compare(0, 3, "\xEF\xBB\xBF") == 0) s.erase(0, 3);
    return s;
}

// Creates parent dirs. Throws on failure.
inline void write_text(const fs::path& path, const std::string& text) {
    if (path.has_parent_path()) fs::create_directories(path.parent_path());
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) throw std::runtime_error("cannot write " + to_utf8(path.wstring()));
    f << text;
    f.close();
    if (f.fail()) throw std::runtime_error("write failed " + to_utf8(path.wstring()));
}

// ---------- versions ----------

// "v1.2.3" / "1.2" ok (max 3 numeric parts). Bad -> 0.0.0, returns false.
inline bool ver_parse(const std::string& s, int v[3]) {
    v[0] = v[1] = v[2] = 0;
    std::string t = trim(s);
    if (!t.empty() && (t[0] == 'v' || t[0] == 'V')) t.erase(0, 1);
    if (t.empty()) return false;
    int n = 0;
    size_t pos = 0;
    for (;;) {
        size_t dot = t.find('.', pos);
        std::string part = t.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
        if (part.empty() || part.size() > 9 || n >= 3 ||
            part.find_first_not_of("0123456789") != std::string::npos) {
            v[0] = v[1] = v[2] = 0;
            return false;
        }
        v[n++] = std::stoi(part);
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
    return true;
}

inline int ver_cmp(const std::string& a, const std::string& b) {
    int x[3], y[3];
    ver_parse(a, x);
    ver_parse(b, y);
    for (int i = 0; i < 3; ++i)
        if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
    return 0;
}

// ---------- SHA-256 (BCrypt) ----------

inline std::string hex_lower(const unsigned char* p, size_t n) {
    static const char* d = "0123456789abcdef";
    std::string s;
    s.reserve(n * 2);
    for (size_t i = 0; i < n; ++i) {
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

class Sha256 {
public:
    Sha256() {
        if (BCryptOpenAlgorithmProvider(&alg_, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
            throw std::runtime_error("sha256 init");
        DWORD len = 0, got = 0;
        BCryptGetProperty(alg_, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&len), sizeof(len), &got, 0);
        obj_.resize(len);
        if (BCryptCreateHash(alg_, &h_, obj_.data(), len, nullptr, 0, 0) != 0)
            throw std::runtime_error("sha256 create");
    }
    ~Sha256() {
        if (h_) BCryptDestroyHash(h_);
        if (alg_) BCryptCloseAlgorithmProvider(alg_, 0);
    }
    Sha256(const Sha256&) = delete;
    Sha256& operator=(const Sha256&) = delete;

    void update(const void* p, size_t n) {
        BCryptHashData(h_, reinterpret_cast<PUCHAR>(const_cast<void*>(p)), static_cast<ULONG>(n), 0);
    }
    std::string finish() {
        unsigned char out[32] = {};
        BCryptFinishHash(h_, out, sizeof out, 0);
        return hex_lower(out, sizeof out);
    }

private:
    BCRYPT_ALG_HANDLE alg_ = nullptr;
    BCRYPT_HASH_HANDLE h_ = nullptr;
    std::vector<UCHAR> obj_;
};

inline std::string sha256_mem(const std::string& data) {
    Sha256 s;
    s.update(data.data(), data.size());
    return s.finish();
}

inline bool sha256_file(const fs::path& path, std::string& hex) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    Sha256 s;
    std::vector<char> buf(64 * 1024);
    while (f) {
        f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        std::streamsize n = f.gcount();
        if (n > 0) s.update(buf.data(), static_cast<size_t>(n));
    }
    hex = s.finish();
    return true;
}

// ---------- paths ----------

struct Paths {
    fs::path win64, ue4ss, mods, core, paks;
};

// win64 = <game>\Windows\MainSequence\Binaries\Win64
inline Paths paths_from_win64(const fs::path& win64_in) {
    fs::path w = win64_in;
    if (w.filename().empty()) w = w.parent_path();  // strip trailing slash
    Paths p;
    p.win64 = w;
    p.ue4ss = w / L"ue4ss";
    p.mods = p.ue4ss / L"Mods";
    p.core = p.mods / L"MSCore";
    p.paks = w.parent_path().parent_path() / L"Content" / L"Paks";
    return p;
}

inline bool valid_game_dir(const fs::path& root) {
    std::error_code ec;
    return fs::is_directory(root / L"Windows" / L"MainSequence" / L"Binaries" / L"Win64", ec);
}

// plain name: no separators, no drive, not . or ..
inline bool safe_name(const std::string& s) {
    return !s.empty() && s != "." && s != ".." && s.find_first_of("/\\:") == std::string::npos;
}

// ---------- network (WinHTTP, https only) ----------

// WHY: updates come only from GitHub hosts; a manifest or redirect pointing anywhere else is refused.
inline bool host_ok(const std::string& host) {
    static const char* allow[] = {"github.com", "objects.githubusercontent.com",
                                  "release-assets.githubusercontent.com",
                                  "github-releases.githubusercontent.com"};
    std::string h = lower(host);
    for (auto a : allow)
        if (h == a) return true;
    return false;
}

// offline only: g_local_root/<owner>_<repo>/ when it holds manifest.txt, else g_local_root/
inline std::string local_dir(const std::string& repo) {
    std::string sub = repo;
    std::replace(sub.begin(), sub.end(), '/', '_');
    fs::path d = fs::path(to_wide(g_local_root)) / to_wide(sub);
    std::error_code ec;
    return fs::exists(d / L"manifest.txt", ec) ? to_utf8(d.wstring()) : g_local_root;
}

inline std::string manifest_url(const std::string& repo) {
    if (g_allow_local && !g_local_root.empty()) return local_dir(repo) + "/manifest.txt";
    // GitHub's CDN caches the asset by URL for a long time; a throwaway query string always gets the current one
    return "https://github.com/" + repo + "/releases/latest/download/manifest.txt?t=" +
           std::to_string(static_cast<long long>(std::time(nullptr)));
}

inline std::string zip_url(const std::string& repo, const std::string& version, const std::string& zip) {
    if (g_allow_local && !g_local_root.empty()) return local_dir(repo) + "/" + zip;
    return "https://github.com/" + repo + "/releases/download/v" + version + "/" + zip;
}

struct WinH {
    HINTERNET h = nullptr;
    ~WinH() {
        if (h) WinHttpCloseHandle(h);
    }
};

struct Url {
    std::wstring host, path;
    INTERNET_PORT port = 0;
    bool https = false;
};

inline bool crack(const std::wstring& u, Url& out) {
    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof uc;
    uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(u.c_str(), static_cast<DWORD>(u.size()), 0, &uc)) return false;
    out.https = uc.nScheme == INTERNET_SCHEME_HTTPS;
    out.host.assign(uc.lpszHostName, uc.dwHostNameLength);
    out.path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.dwExtraInfoLength) out.path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);  // "?query" is split out by WinHTTP; keep it in the request path
    out.port = uc.nPort;
    return true;
}

// Writes to body or to file (one of them). Enforces cap.
class Sink {
public:
    Sink(std::string* body, const fs::path* out, size_t cap) : body_(body), cap_(cap) {
        if (out) {
            if (out->has_parent_path()) fs::create_directories(out->parent_path());
            file_.open(*out, std::ios::binary | std::ios::trunc);
        }
    }
    bool put(const char* p, size_t n) {
        total_ += n;
        if (total_ > cap_) return false;
        if (body_) {
            body_->append(p, n);
            return true;
        }
        file_.write(p, static_cast<std::streamsize>(n));
        return !file_.fail();
    }
    bool close() {
        if (file_.is_open()) file_.close();
        return !file_.fail();
    }
    size_t total() const { return total_; }

private:
    std::string* body_;
    size_t cap_;
    size_t total_ = 0;
    std::ofstream file_;
};

// url without "://" = local path, only when g_allow_local.
inline bool fetch(const std::string& url, std::string* body, const fs::path* out, std::string& err, size_t cap) {
    try {
        if (url.find("://") == std::string::npos) {
            if (!g_allow_local) {
                err = "not https";
                return false;
            }
            std::ifstream f(to_wide(url), std::ios::binary);
            if (!f) {
                err = "missing local file";
                return false;
            }
            Sink sink(body, out, cap);
            std::vector<char> buf(64 * 1024);
            while (f) {
                f.read(buf.data(), static_cast<std::streamsize>(buf.size()));
                std::streamsize n = f.gcount();
                if (n > 0 && !sink.put(buf.data(), static_cast<size_t>(n))) {
                    err = sink.total() > cap ? "too large" : "write failed";
                    return false;
                }
            }
            return sink.close();
        }

        Url u;
        if (!crack(to_wide(url), u)) {
            err = "bad url";
            return false;
        }
        if (!u.https) {
            err = "not https";
            return false;
        }
        if (!host_ok(to_utf8(u.host))) {
            err = "host not allowed";
            return false;
        }

        WinH ses;
        ses.h = WinHttpOpen(L"MSCoreLoader/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
                            WINHTTP_NO_PROXY_BYPASS, 0);
        if (!ses.h) {
            err = "offline";
            return false;
        }
        WinHttpSetTimeouts(ses.h, 5000, 5000, 5000, 5000);
        WinH con;
        con.h = WinHttpConnect(ses.h, u.host.c_str(), u.port, 0);
        if (!con.h) {
            err = "offline";
            return false;
        }
        WinH req;
        req.h = WinHttpOpenRequest(con.h, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                   WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!req.h) {
            err = "request failed";
            return false;
        }
        if (!WinHttpSendRequest(req.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, nullptr, 0, 0, 0) ||
            !WinHttpReceiveResponse(req.h, nullptr)) {
            err = "offline";
            return false;
        }
        DWORD code = 0, sz = sizeof code;
        WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &code, &sz, WINHTTP_NO_HEADER_INDEX);
        if (code != 200) {
            err = "http " + std::to_string(code);
            return false;
        }
        // host after redirects must still be allowed (fail closed)
        wchar_t fin[2048] = {};
        DWORD fl = sizeof fin;
        Url fu;
        if (!WinHttpQueryOption(req.h, WINHTTP_OPTION_URL, fin, &fl) || !crack(std::wstring(fin), fu) ||
            !fu.https || !host_ok(to_utf8(fu.host))) {
            err = "redirect host not allowed";
            return false;
        }

        Sink sink(body, out, cap);
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req.h, &avail)) {
                err = "offline";
                return false;
            }
            if (!avail) break;
            std::string buf(avail, '\0');
            DWORD got = 0;
            if (!WinHttpReadData(req.h, &buf[0], avail, &got)) {
                err = "read failed";
                return false;
            }
            if (got && !sink.put(buf.data(), got)) {
                err = sink.total() > cap ? "too large" : "write failed";
                return false;
            }
        }
        return sink.close();
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

inline bool http_get(const std::string& url, std::string& body, std::string& err, size_t cap = 200u << 20) {
    body.clear();
    return fetch(url, &body, nullptr, err, cap);
}

inline bool http_download(const std::string& url, const fs::path& dest, std::string& err,
                          size_t cap = 200u << 20) {
    return fetch(url, nullptr, &dest, err, cap);
}

// ---------- zip (tar.exe, absolute System32 path: PATH tar may be GNU tar) ----------

// Runs System32\tar.exe with args (no shell). Output = stdout+stderr.
inline bool run_tar(const std::vector<std::wstring>& args, std::string& out, std::string& err) {
    wchar_t sysdir[MAX_PATH] = {};
    GetSystemDirectoryW(sysdir, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(sysdir) + L"\\tar.exe\"";
    for (auto& a : args) cmd += L" \"" + a + L"\"";

    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) {
        err = "pipe failed";
        return false;
    }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = wr;
    si.hStdError = wr;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::vector<wchar_t> mut(cmd.begin(), cmd.end());
    mut.push_back(L'\0');
    BOOL started = CreateProcessW(nullptr, mut.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                  nullptr, &si, &pi);
    CloseHandle(wr);
    if (!started) {
        CloseHandle(rd);
        err = "tar.exe not found";
        return false;
    }
    std::string data;
    char buf[4096];
    DWORD n = 0;
    while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n) data.append(buf, n);
    CloseHandle(rd);
    WaitForSingleObject(pi.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    out = data;
    if (code != 0) {
        err = "tar exit " + std::to_string(code) + ": " + trim(data);
        return false;
    }
    return true;
}

inline std::string norm_slash(std::string s) {
    std::replace(s.begin(), s.end(), '\\', '/');
    return s;
}

// relative, '/' separated, no drive, no empty/./.. segment, not absolute
inline bool safe_rel(const std::string& rel) {
    if (rel.empty() || rel[0] == '/' || rel.find(':') != std::string::npos) return false;
    size_t p = 0;
    for (;;) {
        size_t s = rel.find('/', p);
        std::string seg = rel.substr(p, s == std::string::npos ? std::string::npos : s - p);
        if (seg.empty() || seg == "." || seg == "..") return false;
        if (s == std::string::npos) return true;
        p = s + 1;
    }
}

inline bool zip_entries_ok(const std::vector<std::string>& entries, std::string& err) {
    static const std::set<std::string> exts = {".dll", ".lua", ".pak", ".utoc", ".ucas", ".txt", ".mod", ".ini"};
    for (const auto& orig : entries) {
        std::string e = norm_slash(orig);
        bool dir = !e.empty() && e.back() == '/';
        if (dir) e.pop_back();
        if (!safe_rel(e)) {
            err = "bad path: " + orig;
            return false;
        }
        size_t s = e.find('/');
        std::string root = e.substr(0, s);
        if (root != "Mods" && root != "Paks") {
            err = "root not allowed: " + orig;
            return false;
        }
        if (!dir) {
            size_t dot = e.rfind('.');
            size_t last = e.rfind('/');
            std::string ext = (dot == std::string::npos || (last != std::string::npos && dot < last))
                                  ? ""
                                  : lower(e.substr(dot));
            if (!exts.count(ext)) {
                err = "file type not allowed: " + orig;
                return false;
            }
        }
    }
    return true;
}

inline bool zip_list(const fs::path& zip, std::vector<std::string>& entries, std::string& err) {
    std::string out;
    if (!run_tar({L"-tf", zip.wstring()}, out, err)) return false;
    entries.clear();
    std::istringstream in(out);
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (!line.empty()) entries.push_back(line);
    }
    return true;
}

// list -> validate -> extract into dest
inline bool zip_extract(const fs::path& zip, const fs::path& dest, std::string& err) {
    std::vector<std::string> entries;
    if (!zip_list(zip, entries, err) || !zip_entries_ok(entries, err)) return false;
    std::error_code ec;
    fs::create_directories(dest, ec);
    std::wstring d = dest.wstring();
    while (!d.empty() && (d.back() == L'\\' || d.back() == L'/')) d.pop_back();
    std::string out;
    return run_tar({L"-xf", zip.wstring(), L"-C", d}, out, err);
}

// ---------- mods / manifest / check ----------

struct Mod {
    std::string name, repo, version;
    fs::path dir;
};

inline std::vector<Mod> scan_mods(const Paths& p) {
    std::vector<Mod> mods;
    std::error_code ec;
    for (auto& d : fs::directory_iterator(p.mods, ec)) {
        if (!d.is_directory(ec)) continue;
        fs::path mf = d.path() / L"mscore.mod";
        if (!fs::exists(mf, ec)) continue;
        KV kv = parse_kv(read_text(mf));
        Mod m;
        m.dir = d.path();
        m.name = kv["name"].empty() ? to_utf8(d.path().filename().wstring()) : kv["name"];
        m.repo = kv["repo"];
        m.version = kv["version"];
        mods.push_back(m);
    }
    std::sort(mods.begin(), mods.end(), [](const Mod& a, const Mod& b) { return a.name < b.name; });
    return mods;
}

struct Manifest {
    std::string name, version, zip, sha256, min_native, ue4ss_version, ue4ss_zip, ue4ss_sha256;
    long long size = 0, ue4ss_size = 0;
};

inline bool parse_manifest(const std::string& text, Manifest& m, std::string& err) {
    KV kv = parse_kv(text);
    m = Manifest{};
    for (const char* k : {"name", "version", "zip", "sha256"}) {
        if (kv[k].empty()) {
            err = std::string("manifest missing ") + k;
            return false;
        }
    }
    m.name = kv["name"];
    m.version = kv["version"];
    m.zip = kv["zip"];
    m.sha256 = kv["sha256"];
    m.size = std::strtoll(kv["size"].c_str(), nullptr, 10);
    m.min_native = kv["min_mscore_native"];
    m.ue4ss_version = kv["ue4ss_version"];
    m.ue4ss_zip = kv["ue4ss_zip"];
    m.ue4ss_sha256 = kv["ue4ss_sha256"];
    m.ue4ss_size = std::strtoll(kv["ue4ss_size"].c_str(), nullptr, 10);
    return true;
}

struct Check {
    Mod mod;
    Manifest m;
    std::string state;  // uptodate | update | error:<msg> | skipped:min_native
};

inline Check check_mod(const Mod& mod, const std::string& core_native_ver) {
    Check c;
    c.mod = mod;
    std::string text, err;
    if (!http_get(manifest_url(mod.repo), text, err, 1u << 20)) {
        c.state = "error:" + err;
        return c;
    }
    if (!parse_manifest(text, c.m, err)) {
        c.state = "error:" + err;
        return c;
    }
    if (!c.m.min_native.empty() && ver_cmp(core_native_ver, c.m.min_native) < 0) {
        c.state = "skipped:min_native";
        return c;
    }
    c.state = ver_cmp(c.m.version, mod.version) > 0 ? "update" : "uptodate";
    return c;
}

// zip-relative "Mods/x/y.dll" or "Paks/p.pak" -> real target. Empty path = refused.
inline fs::path target_for(const Paths& p, const std::string& rel_in) {
    std::string rel = norm_slash(rel_in);
    if (!safe_rel(rel)) return {};
    size_t s = rel.find('/');
    if (s == std::string::npos) return {};
    std::string root = rel.substr(0, s), rest = rel.substr(s + 1);
    if (root == "Mods") return p.mods / to_wide(rest);
    if (root == "Paks") return p.paks / to_wide(rest);
    return {};
}

// never touched by an update
inline bool protected_name(const std::string& file_name) {
    std::string n = lower(file_name);
    return n == "miningbench.ini" || n == "loader.ini" || (n.size() > 4 && n.substr(n.size() - 4) == ".log");
}

// download zip -> hash/size check -> extract to updates\<mod>\<ver> -> optional on_staged(stage) -> pending.txt
inline bool stage_update(const Paths& p, const Mod& mod, const Manifest& m, std::string& err,
                         const std::function<bool(const fs::path&, std::string&)>& on_staged = {}) {
    try {
        if (!safe_name(mod.name) || !safe_name(m.version) || !safe_name(m.zip)) {
            err = "bad name";
            return false;
        }
        fs::path zip = p.core / L"tmp" / to_wide(m.zip);
        struct Cleanup {
            fs::path f;
            ~Cleanup() {
                std::error_code ec;
                fs::remove(f, ec);
            }
        } cleanup{zip};
        if (!http_download(zip_url(mod.repo, m.version, m.zip), zip, err)) return false;

        std::string hex;
        if (!sha256_file(zip, hex) || lower(hex) != lower(m.sha256)) {
            err = "hash mismatch";
            return false;
        }
        if (m.size > 0 && static_cast<long long>(fs::file_size(zip)) != m.size) {
            err = "size mismatch";
            return false;
        }

        fs::path stage = p.core / L"updates" / to_wide(mod.name) / to_wide(m.version);
        std::error_code ec;
        fs::remove_all(stage, ec);
        if (!zip_extract(zip, stage, err)) return false;
        if (on_staged && !on_staged(stage, err)) {
            fs::remove_all(stage, ec);
            return false;
        }

        // one pending line per mod: drop older lines for this mod, append new
        fs::path pend = p.core / L"pending.txt";
        std::string keep;
        std::istringstream in(read_text(pend));
        std::string l;
        while (std::getline(in, l)) {
            std::string t = trim(l);
            if (t.empty() || t.substr(0, t.find('|')) == mod.name) continue;
            keep += t + "\n";
        }
        write_text(pend, keep + mod.name + "|" + m.version + "\n");
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

// Applies pending.txt: old file -> backup\<mod>\<rel>, new file moved in. Returns mods applied.
inline int apply_pending(const Paths& p, const std::function<void(const std::string&)>& log) {
    auto say = [&](const std::string& s) {
        if (log) log(s);
    };
    fs::path pend = p.core / L"pending.txt";
    std::string text = read_text(pend);
    if (text.empty()) return 0;
    int applied = 0;
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l)) {
        std::string t = trim(l);
        size_t bar = t.find('|');
        if (t.empty() || bar == std::string::npos) continue;
        std::string mod = t.substr(0, bar), ver = t.substr(bar + 1);
        if (!safe_name(mod) || !safe_name(ver)) {
            say("[MSCore] bad pending line: " + t);
            continue;
        }
        fs::path stage = p.core / L"updates" / to_wide(mod) / to_wide(ver);
        std::error_code ec;
        if (!fs::is_directory(stage, ec)) {
            say("[MSCore] staged update missing: " + t);
            continue;
        }
        std::vector<std::string> done;  // zip-relative paths moved in so far
        // installed\<mod>.txt: line 1 = version, then every file this mod owns (union with older versions' list)
        auto record = [&]() {
            std::set<std::string> all(done.begin(), done.end());
            std::istringstream old(read_text(p.core / L"installed" / to_wide(mod + ".txt")));
            std::string o;
            std::getline(old, o);  // old version line
            while (std::getline(old, o))
                if (!trim(o).empty()) all.insert(trim(o));
            std::string out = ver + "\n";
            for (const auto& r : all) out += r + "\n";
            write_text(p.core / L"installed" / to_wide(mod + ".txt"), out);
        };
        try {
            std::vector<fs::path> files;
            for (auto& e : fs::recursive_directory_iterator(stage))
                if (e.is_regular_file()) files.push_back(e.path());
            for (auto& f : files) {
                std::string rel = norm_slash(to_utf8(fs::relative(f, stage).wstring()));
                std::string fname = to_utf8(f.filename().wstring());
                fs::path target = target_for(p, rel);
                if (target.empty() || protected_name(fname)) {
                    say("[MSCore] skipped " + rel);
                    continue;
                }
                if (fs::exists(target)) {
                    fs::path bk = p.core / L"backup" / to_wide(mod) / to_wide(rel);
                    fs::create_directories(bk.parent_path());
                    fs::remove(bk, ec);
                    fs::rename(target, bk);  // loaded DLLs can be renamed, not deleted
                }
                fs::create_directories(target.parent_path());
                fs::rename(f, target);
                done.push_back(rel);
            }
            record();
            fs::remove_all(stage, ec);
            ++applied;
            say("[MSCore] applied " + mod + " " + ver);
        } catch (const std::exception& e) {
            say("[MSCore] apply failed " + mod + ": " + e.what());
            if (!done.empty()) record();  // keep the list of what did get moved in
        }
    }
    std::error_code ec;
    fs::remove(pend, ec);
    return applied;
}

// Restores the backup of mod. Current file renamed to <file>.old first.
inline bool rollback(const Paths& p, const std::string& mod, std::string& err) {
    try {
        if (!safe_name(mod)) {
            err = "bad mod name";
            return false;
        }
        fs::path bdir = p.core / L"backup" / to_wide(mod);
        std::error_code ec;
        if (!fs::is_directory(bdir, ec)) {
            err = "no backup for " + mod;
            return false;
        }
        std::vector<fs::path> files;
        for (auto& e : fs::recursive_directory_iterator(bdir))
            if (e.is_regular_file()) files.push_back(e.path());
        for (auto& f : files) {
            std::string rel = norm_slash(to_utf8(fs::relative(f, bdir).wstring()));
            fs::path target = target_for(p, rel);
            if (target.empty()) continue;
            if (fs::exists(target)) {
                fs::path old = target;
                old += L".old";
                fs::remove(old, ec);
                fs::rename(target, old);
            }
            fs::create_directories(target.parent_path());
            fs::rename(f, target);
        }
        fs::remove_all(bdir, ec);
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

// ---------- repo ----------

// owner/repo with GitHub name chars; "." and ".." segments refused (they would change the URL path).
inline bool valid_repo(const std::string& r) {
    static const std::regex re("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$");
    if (!std::regex_match(r, re)) return false;
    size_t s = r.find('/');
    auto dots = [](const std::string& x) { return x == "." || x == ".."; };
    return !dots(r.substr(0, s)) && !dots(r.substr(s + 1));
}

// owner/repo, or a github.com URL: http(s)://[www.]github.com/owner/repo[.git][/][/releases[/tag/x]|/tree/...][?q][#f],
// or github.com/owner/repo / www.github.com/owner/repo without scheme. Any other host (userinfo, port,
// github.com.evil.com...) is refused. Returns owner/repo, or "" when refused.
inline std::string parse_repo(const std::string& in) {
    std::string s = trim(in), low = lower(s), rest = s;
    size_t sch = low.find("://");
    if (sch != std::string::npos) {
        if (low.substr(0, sch) != "https" && low.substr(0, sch) != "http") return "";
        std::string after = s.substr(sch + 3);
        size_t end = after.find_first_of("/?#");
        std::string host = lower(end == std::string::npos ? after : after.substr(0, end));
        if (host != "github.com" && host != "www.github.com") return "";
        rest = end == std::string::npos ? "" : after.substr(end);
    } else {
        for (const char* pre : {"www.github.com/", "github.com/"}) {
            size_t n = std::strlen(pre);
            if (low.compare(0, n, pre) == 0) {
                rest = s.substr(n);
                break;
            }
        }
    }
    size_t cut = rest.find_first_of("?#");
    if (cut != std::string::npos) rest = rest.substr(0, cut);
    while (!rest.empty() && rest.front() == '/') rest.erase(0, 1);
    while (!rest.empty() && rest.back() == '/') rest.pop_back();

    std::vector<std::string> seg;
    std::istringstream in2(rest);
    for (std::string part; std::getline(in2, part, '/');) seg.push_back(part);
    if (seg.size() < 2) return "";
    if (seg[1].size() > 4 && seg[1].compare(seg[1].size() - 4, 4, ".git") == 0) seg[1].resize(seg[1].size() - 4);
    if (seg.size() > 2) {
        if (seg[2] == "releases") {
            if (!(seg.size() == 3 || (seg.size() == 5 && seg[3] == "tag"))) return "";
        } else if (seg[2] != "tree") {
            return "";
        }
    }
    std::string repo = seg[0] + "/" + seg[1];
    return valid_repo(repo) ? repo : "";
}

// ---------- remove mod ----------

struct RemoveItem {
    std::string rel;  // zip-relative "Mods/..." or "Paks/..."
    fs::path target;  // real file
    fs::path root;    // game Mods or Paks dir: never removed, only empty dirs below it
};

// f strictly below root (case-insensitive, lexical)
inline bool under_root(const fs::path& f, const fs::path& root) {
    auto low = [](std::wstring s) {
        for (auto& c : s)
            if (c >= L'A' && c <= L'Z') c = static_cast<wchar_t>(c + 32);
        return s;
    };
    std::wstring a = low(f.lexically_normal().wstring()), b = low(root.lexically_normal().wstring());
    if (b.empty() || b.back() != L'\\') b += L'\\';
    return a.size() > b.size() && a.compare(0, b.size(), b) == 0;
}

// Files listed in installed\<mod>.txt that are safe to delete. Touches nothing.
inline bool remove_plan(const Paths& p, const std::string& mod, std::vector<RemoveItem>& items, std::string& err) {
    items.clear();
    if (!safe_name(mod) || lower(mod) == "mscore") {
        err = "refusing to remove " + mod;
        return false;
    }
    std::string text = read_text(p.core / L"installed" / to_wide(mod + ".txt"));
    if (text.empty()) {
        // no install list: the mod folder is the list
        fs::path d = p.mods / to_wide(mod);
        std::error_code ec;
        if (!fs::is_regular_file(d / L"mscore.mod", ec)) {
            err = mod + " is not installed";
            return false;
        }
        text = "0\n";
        for (auto it = fs::recursive_directory_iterator(d, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec))
            if (it->is_regular_file(ec)) text += "Mods/" + to_utf8(fs::relative(it->path(), p.mods).generic_wstring()) + "\n";
    }
    std::istringstream in(text);
    std::string l;
    std::getline(in, l);  // version line
    while (std::getline(in, l)) {
        std::string rel = norm_slash(trim(l));
        if (rel.empty()) continue;
        fs::path t = target_for(p, rel);
        if (t.empty() || protected_name(to_utf8(t.filename().wstring()))) continue;
        fs::path root = rel.compare(0, 5, "Mods/") == 0 ? p.mods : p.paks;
        if (!under_root(t, root)) continue;
        items.push_back({rel, t, root});
    }
    if (items.empty()) {
        err = mod + " install list has no files (recorded by an older build); remove its files by hand";
        return false;
    }
    return true;
}

// Drops lines pred() accepts; every other byte stays. Writes only when something was dropped.
inline bool drop_lines(const fs::path& f, const std::function<bool(const std::string&)>& pred) {
    std::string text = read_text(f), out;
    bool any = false;
    size_t pos = 0;
    for (;;) {
        size_t e = text.find('\n', pos);
        bool last = e == std::string::npos;
        std::string line = text.substr(pos, last ? std::string::npos : e - pos);
        if (pred(line)) {
            any = true;
        } else {
            out += line;
            if (!last) out += '\n';
        }
        if (last) break;
        pos = e + 1;
    }
    if (any) write_text(f, out);
    return any;
}

// mods.txt "mod : v" line (comments and blanks never match). val = trimmed value.
inline bool mods_txt_match(const std::string& line, const std::string& mod, std::string& val) {
    std::string t = trim(line);
    if (t.empty() || t[0] == '#' || t[0] == ';') return false;
    size_t c = t.find(':');
    if (c == std::string::npos || lower(trim(t.substr(0, c))) != lower(mod)) return false;
    val = trim(t.substr(c + 1));
    return true;
}

inline bool mods_txt_line(const std::string& text, const std::string& mod, std::string& val) {
    std::istringstream in(text);
    std::string l;
    while (std::getline(in, l))
        if (mods_txt_match(l, mod, val)) return true;
    return false;
}

// First matching line becomes "<mod> : <v>"; CR of a CRLF line kept; other bytes untouched.
inline std::string mods_txt_set(const std::string& text, const std::string& mod, const std::string& v) {
    std::string out, val;
    bool done = false;
    size_t pos = 0;
    for (;;) {
        size_t e = text.find('\n', pos);
        bool last = e == std::string::npos;
        std::string line = text.substr(pos, last ? std::string::npos : e - pos);
        if (!done && mods_txt_match(line, mod, val)) {
            out += mod + " : " + v + (!line.empty() && line.back() == '\r' ? "\r" : "");
            done = true;
        } else {
            out += line;
        }
        if (last) break;
        out += '\n';
        pos = e + 1;
    }
    return out;
}

// Enabled = enabled.txt exists, or mods.txt says "mod : 1".
inline bool mod_enabled(const Paths& p, const std::string& mod) {
    std::error_code ec;
    if (fs::is_regular_file(p.mods / to_wide(mod) / L"enabled.txt", ec)) return true;
    std::string val;
    return mods_txt_line(read_text(p.mods / L"mods.txt"), mod, val) && !val.empty() && val[0] == '1';
}

// enabled.txt (UE4SS starts it even if mods.txt says 0) + mods.txt line if one exists (never adds one).
// Refuses MSCore. The caller must check the game is not running.
inline bool set_mod_enabled(const Paths& p, const std::string& mod, bool on, std::string& err) {
    if (!safe_name(mod) || lower(mod) == "mscore") {
        err = "refusing to toggle " + mod;
        return false;
    }
    try {
        if (on) {
            write_text(p.mods / to_wide(mod) / L"enabled.txt", "");
        } else {
            std::error_code ec;
            fs::remove(p.mods / to_wide(mod) / L"enabled.txt", ec);
        }
        fs::path txt = p.mods / L"mods.txt";
        std::string text = read_text(txt), val;
        if (mods_txt_line(text, mod, val)) write_text(txt, mods_txt_set(text, mod, on ? "1" : "0"));
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

// Deletes the files listed for mod. A file that was overwritten by an update is put back from backup\.
// Then: install list, backup dir, staged update, pending line, legacy loader.ini force_install.<mod> lines, mods.txt line.
// Refuses MSCore, refuses paths outside Mods\ and Paks\, never touches protected/user files (see protected_name).
// The caller must check the game is not running.
inline bool remove_mod(const Paths& p, const std::string& mod, std::string& err) {
    std::vector<RemoveItem> items;
    if (!remove_plan(p, mod, items, err)) return false;
    try {
        std::error_code ec;
        size_t failed = 0;
        fs::path bdir = p.core / L"backup" / to_wide(mod);
        for (const auto& it : items) {
            fs::path bk = bdir / to_wide(it.rel);
            bool has_bk = fs::is_regular_file(bk, ec);
            if (fs::exists(it.target, ec) && !fs::remove(it.target, ec)) {
                ++failed;  // locked
                continue;
            }
            if (has_bk) {
                fs::create_directories(it.target.parent_path(), ec);
                fs::rename(bk, it.target, ec);
                if (ec) ++failed;
            }
        }
        for (const auto& it : items)
            for (fs::path d = it.target.parent_path(); under_root(d, it.root); d = d.parent_path())
                if (!fs::remove(d, ec)) break;  // not empty (or gone): stop climbing
        if (failed) {
            err = std::to_string(failed) + " file(s) could not be removed (locked?); nothing else changed, run Remove again";
            return false;
        }

        fs::remove_all(bdir, ec);
        fs::remove_all(p.core / L"updates" / to_wide(mod), ec);
        fs::remove(p.core / L"installed" / to_wide(mod + ".txt"), ec);
        drop_lines(p.core / L"pending.txt", [&](const std::string& l) {
            std::string t = trim(l);
            return !t.empty() && t.substr(0, t.find('|')) == mod;
        });
        drop_lines(p.core / L"loader.ini", [&](const std::string& l) {
            size_t eq = l.find('=');
            return eq != std::string::npos && trim(l.substr(0, eq)) == "force_install." + mod;
        });
        drop_lines(p.mods / L"mods.txt", [&](const std::string& l) {
            std::string t = trim(l);
            if (t.empty() || t[0] == '#' || t[0] == ';') return false;
            size_t c = t.find(':');
            return c != std::string::npos && lower(trim(t.substr(0, c))) == lower(mod);
        });
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

// ---------- Steam / UE4SS mods.txt helpers ----------

// Steam libraryfolders.vdf: every "path" value, \\ unescaped.
inline std::vector<std::wstring> vdf_paths(const std::string& vdf) {
    static const std::regex re("\"path\"\\s+\"((?:[^\"\\\\]|\\\\.)*)\"", std::regex::icase);
    std::vector<std::wstring> out;
    for (std::sregex_iterator it(vdf.begin(), vdf.end(), re), end; it != end; ++it) {
        std::string v = (*it)[1].str(), u;
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] == '\\' && i + 1 < v.size()) u += v[++i];
            else u += v[i];
        }
        out.push_back(to_wide(u));
    }
    return out;
}

// Removes every "MSCore :" line, inserts `line` before the first non-comment line.
// Keeps CRLF/LF style. Idempotent.
inline std::string mods_txt_insert_first(const std::string& text, const std::string& line) {
    std::string nl = text.find("\r\n") != std::string::npos ? "\r\n" : "\n";
    std::vector<std::string> lines;
    size_t p = 0;
    for (;;) {
        size_t e = text.find('\n', p);
        if (e == std::string::npos) {
            lines.push_back(text.substr(p));
            break;
        }
        lines.push_back(text.substr(p, e - p));
        p = e + 1;
    }
    auto is_core = [](const std::string& l) {
        std::string t = trim(l);
        if (t.compare(0, 6, "MSCore") != 0) return false;
        size_t i = 6;
        while (i < t.size() && t[i] == ' ') ++i;
        return i < t.size() && t[i] == ':';
    };
    std::vector<std::string> out;
    for (auto& l : lines)
        if (!is_core(l)) out.push_back(l);

    size_t idx = std::string::npos;
    for (size_t i = 0; i < out.size(); ++i) {
        std::string t = trim(out[i]);
        if (!t.empty() && t[0] != '#' && t[0] != ';') {
            idx = i;
            break;
        }
    }
    if (idx == std::string::npos) idx = out.empty() ? 0 : out.size() - (trim(out.back()).empty() ? 1 : 0);

    out.insert(out.begin() + static_cast<std::ptrdiff_t>(idx), line + (nl == "\r\n" ? "\r" : ""));
    std::string r;
    for (size_t i = 0; i < out.size(); ++i) {
        r += out[i];
        if (i + 1 < out.size()) r += "\n";
    }
    return r;
}

}  // namespace mscl
