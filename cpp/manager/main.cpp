// MSCoreManager.exe - install / check / update / rollback MSCore + mods. Plain Win32 controls.
// CLI: --game <Win64 dir>  --source <dir with manifest.txt + zips> (offline, tests)  --install (headless)
//      --add <owner/repo> (headless; with --source: <source>/<owner>_<repo>/manifest.txt if present, else <source>/manifest.txt)
//      --remove <ModName> (headless; deletes only the files listed in ue4ss\Mods\MSCore\installed\<mod>.txt)
// Headless log: %TEMP%\MSCoreManager.log
#include "msc_loader.hpp"

#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <stdlib.h>
#include <tlhelp32.h>
#include <wchar.h>
#include <wctype.h>

#include <thread>

namespace fs = std::filesystem;

namespace {

const wchar_t* kClass = L"MSCoreManagerWnd";
const char* kRepo = "xxalex3xx97/MSCore";
const UINT WM_APP_CHECKED = WM_APP + 1;
const UINT WM_APP_WORK = WM_APP + 2;

enum : int { ID_LIST = 100, ID_CHECK, ID_UPD_SEL, ID_UPD_ALL, ID_INSTALL, ID_ROLLBACK, ID_BROWSE, ID_GAME, ID_STATUS, ID_ADD, ID_REMOVE, ID_TOGGLE };
const int IDD_ADDMOD = 300, IDC_REPO = 301;  // manager.rc DIALOG

HWND g_hwnd = nullptr, g_list = nullptr, g_game_lbl = nullptr, g_status = nullptr;
HWND g_btn_check = nullptr, g_btn_add = nullptr, g_btn_upd_sel = nullptr, g_btn_upd_all = nullptr,
     g_btn_install = nullptr, g_btn_rollback = nullptr, g_btn_browse = nullptr, g_btn_remove = nullptr,
     g_btn_toggle = nullptr;
std::string g_add_repo;  // set by the Add mod dialog
mscl::Paths g_paths;
bool g_has_game = false;
bool g_busy = false;
std::string g_last;  // last work result, shown together with the next check result
std::vector<mscl::Check> g_rows;

// ---------- log / status ----------

void log_line(const std::string& s) {
    wchar_t tmp[MAX_PATH];
    GetTempPathW(MAX_PATH, tmp);
    std::ofstream f(fs::path(tmp) / L"MSCoreManager.log", std::ios::app | std::ios::binary);
    f << s << "\n";
}

void set_status(const std::string& s) {
    if (g_status) SetWindowTextW(g_status, mscl::to_wide(s).c_str());
}

// ---------- game location ----------

std::wstring norm_w(std::wstring s) {
    for (auto& c : s) c = (c == L'/') ? L'\\' : static_cast<wchar_t>(towlower(c));
    return s;
}

// True if a MainSequence* process runs from under win64. Fails closed (true) on API errors.
bool game_running_under(const fs::path& win64) {
    std::wstring base = norm_w(win64.wstring());
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return true;
    bool hit = false;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof pe;
    for (BOOL ok = Process32FirstW(snap, &pe); ok && !hit; ok = Process32NextW(snap, &pe)) {
        if (_wcsnicmp(pe.szExeFile, L"MainSequence", 12) != 0) continue;
        HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pe.th32ProcessID);
        if (!h) {
            hit = true;
            break;
        }
        wchar_t img[2048];
        DWORD n = 2048;
        if (!QueryFullProcessImageNameW(h, 0, img, &n)) hit = true;
        else hit = norm_w(std::wstring(img, n)).compare(0, base.size(), base) == 0;
        CloseHandle(h);
    }
    CloseHandle(snap);
    return hit;
}

bool game_running() { return g_has_game && game_running_under(g_paths.win64); }

fs::path settings_file() {
    const wchar_t* a = _wgetenv(L"APPDATA");
    return fs::path(a ? a : L".") / L"MSCoreManager" / L"game.txt";
}

fs::path saved_game() {
    std::string t = mscl::trim(mscl::read_text(settings_file()));
    return t.empty() ? fs::path() : fs::path(mscl::to_wide(t));
}

// First Main Sequence* install under a Steam library with a Win64 dir. Empty = not found.
fs::path detect_game() {
    std::vector<fs::path> libs;
    wchar_t buf[MAX_PATH * 4];
    DWORD sz = sizeof buf;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Valve\\Steam", L"SteamPath", RRF_RT_REG_SZ, nullptr, buf, &sz) ==
        ERROR_SUCCESS)
        libs.push_back(buf);
    sz = sizeof buf;
    if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\WOW6432Node\\Valve\\Steam", L"InstallPath", RRF_RT_REG_SZ, nullptr,
                     buf, &sz) == ERROR_SUCCESS)
        libs.push_back(buf);
    for (size_t i = 0; i < libs.size(); ++i) {
        std::string vdf = mscl::read_text(libs[i] / L"steamapps" / L"libraryfolders.vdf");
        for (auto& w : mscl::vdf_paths(vdf)) {
            fs::path np(w);
            bool dup = false;
            for (auto& l : libs) dup = dup || _wcsicmp(l.lexically_normal().c_str(), np.lexically_normal().c_str()) == 0;
            if (!dup) libs.push_back(np);
        }
    }
    for (auto& lib : libs) {
        std::error_code ec;
        fs::directory_iterator it(lib / L"steamapps" / L"common", ec), end;
        if (ec) continue;
        for (; it != end; it.increment(ec)) {
            if (ec) break;
            std::wstring name = it->path().filename().wstring();
            if (_wcsnicmp(name.c_str(), L"Main Sequence", 13) != 0) continue;
            fs::path w = it->path() / L"Windows" / L"MainSequence" / L"Binaries" / L"Win64";
            if (fs::is_directory(w, ec)) return w;
        }
    }
    return {};
}

void apply_game(const fs::path& win64) {
    g_paths = mscl::paths_from_win64(win64);
    g_has_game = true;
}

// root = Main Sequence folder (has Windows\MainSequence\Binaries\Win64). Optionally remembers it.
bool set_game_root(const fs::path& root, bool save) {
    if (!mscl::valid_game_dir(root)) return false;
    apply_game(root / L"Windows" / L"MainSequence" / L"Binaries" / L"Win64");
    if (save) {
        try {
            mscl::write_text(settings_file(), mscl::to_utf8(g_paths.win64.wstring()) + "\n");
        } catch (const std::exception& e) {
            log_line(std::string("save game.txt: ") + e.what());
        }
    }
    return true;
}

bool pick_folder(std::wstring& out) {
    IFileDialog* d = nullptr;
    if (FAILED(CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&d)))) return false;
    DWORD opts = 0;
    d->GetOptions(&opts);
    d->SetOptions(opts | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM);
    d->SetTitle(L"Select the Main Sequence game folder");
    bool ok = false;
    if (SUCCEEDED(d->Show(g_hwnd))) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(d->GetResult(&item))) {
            PWSTR p = nullptr;
            if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &p))) {
                out = p;
                CoTaskMemFree(p);
                ok = true;
            }
            item->Release();
        }
    }
    d->Release();
    return ok;
}

// ---------- install (shared by --install and the Install button) ----------

// ue4ss zip: dwmapi.dll, ue4ss/UE4SS.dll, UE4SS-settings.ini (kept if present), license files.
// Only these named files are copied, so zip paths outside them are never written.
bool install_ue4ss(const mscl::Paths& p, const mscl::Manifest& m, std::string& err) {
    if (!mscl::safe_name(m.ue4ss_zip)) {
        err = "bad ue4ss_zip name";
        return false;
    }
    fs::path tmp = p.core / L"tmp";
    fs::path zip = tmp / mscl::to_wide(m.ue4ss_zip);
    fs::path stage = tmp / L"ue4ss";
    struct Cleanup {
        fs::path z, s;
        ~Cleanup() {
            std::error_code ec;
            fs::remove(z, ec);
            fs::remove_all(s, ec);
        }
    } cleanup{zip, stage};
    try {
        if (!mscl::http_download(mscl::zip_url(kRepo, m.version, m.ue4ss_zip), zip, err)) return false;
        std::string hex;
        if (!mscl::sha256_file(zip, hex) || mscl::lower(hex) != mscl::lower(m.ue4ss_sha256)) {
            err = "ue4ss hash mismatch";
            return false;
        }
        if (m.ue4ss_size > 0 && static_cast<long long>(fs::file_size(zip)) != m.ue4ss_size) {
            err = "ue4ss size mismatch";
            return false;
        }
        std::vector<std::string> entries;
        if (!mscl::zip_list(zip, entries, err)) return false;
        for (auto& e : entries) {
            std::string r = mscl::norm_slash(e);
            if (!r.empty() && r.back() == '/') r.pop_back();
            if (!mscl::safe_rel(r)) {
                err = "bad ue4ss path: " + e;
                return false;
            }
        }
        fs::create_directories(stage);
        std::string out;
        if (!mscl::run_tar({L"-xf", zip.wstring(), L"-C", stage.wstring()}, out, err)) return false;

        auto find = [&](const std::string& name) {
            fs::path a = stage / mscl::to_wide(name), b = stage / L"ue4ss" / mscl::to_wide(name);
            std::error_code ec;
            return fs::is_regular_file(a, ec) ? a : (fs::is_regular_file(b, ec) ? b : fs::path());
        };
        fs::path dll = find("dwmapi.dll"), ue = find("UE4SS.dll"), set = find("UE4SS-settings.ini");
        if (dll.empty() || ue.empty()) {
            err = "ue4ss zip missing dwmapi.dll or UE4SS.dll";
            return false;
        }
        const auto ow = fs::copy_options::overwrite_existing;
        fs::create_directories(p.ue4ss);
        fs::copy_file(dll, p.win64 / L"dwmapi.dll", ow);
        fs::copy_file(ue, p.ue4ss / L"UE4SS.dll", ow);
        if (!set.empty() && !fs::exists(p.ue4ss / L"UE4SS-settings.ini")) fs::copy_file(set, p.ue4ss / L"UE4SS-settings.ini");
        for (const char* n : {"UE4SS-LICENSE.txt", "THIRD-PARTY.txt"}) {
            fs::path f = find(n);
            if (!f.empty()) fs::copy_file(f, p.ue4ss / mscl::to_wide(n), ow);
        }
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

// mods.txt: drop old "MSCore :" lines, insert "MSCore : 1" as first non-comment line.
bool set_mods_txt(const mscl::Paths& p, std::string& err) {
    fs::path f = p.mods / L"mods.txt";
    try {
        mscl::write_text(f, mscl::mods_txt_insert_first(mscl::read_text(f), "MSCore : 1"));
        return true;
    } catch (const std::exception& e) {
        err = e.what();
        return false;
    }
}

// Game must be closed (caller checks). Manifest -> ue4ss + MSCore zips (hash checked) -> files -> mods.txt.
bool install_core(const mscl::Paths& p, std::string& err) {
    std::string text;
    if (!mscl::http_get(mscl::manifest_url(kRepo), text, err, 1u << 20)) return false;
    mscl::Manifest m;
    if (!mscl::parse_manifest(text, m, err)) return false;
    if (m.ue4ss_zip.empty()) {
        err = "manifest missing ue4ss_zip";
        return false;
    }
    if (!install_ue4ss(p, m, err)) return false;
    mscl::Mod core;
    core.name = "MSCore";
    core.repo = kRepo;
    core.version = "0.0.0";
    if (!mscl::stage_update(p, core, m, err)) return false;
    int n = mscl::apply_pending(p, log_line);
    log_line("applied " + std::to_string(n) + " update(s)");
    return set_mods_txt(p, err);
}

int install_cli(const fs::path& win64) {
    if (game_running_under(win64)) {
        log_line("install: game is running - close it first");
        return 1;
    }
    std::string err;
    if (!install_core(mscl::paths_from_win64(win64), err)) {
        log_line("install: FAILED " + err);
        return 1;
    }
    log_line("install: OK");
    return 0;
}

// ---------- add mod (latest GitHub release of owner/repo) ----------

// Game must be closed (caller checks). manifest -> min_native -> zip (hash, entries, Mods/<Name>/mscore.mod repo
// must equal repo) -> pending -> apply (same backup logic as updates). mods.txt is never edited here.
bool add_mod_core(const mscl::Paths& p, const std::string& repo, std::string& msg, std::string& err) {
    if (!mscl::valid_repo(repo)) {
        err = "repo must be owner/repo";
        return false;
    }
    std::string text;
    if (!mscl::http_get(mscl::manifest_url(repo), text, err, 1u << 20)) return false;
    mscl::Manifest m;
    if (!mscl::parse_manifest(text, m, err)) return false;
    if (!mscl::safe_name(m.name)) {
        err = "bad mod name in manifest";
        return false;
    }
    std::string core = "0.0.0";
    for (const auto& mod : mscl::scan_mods(p))
        if (mod.name == "MSCore") core = mod.version;
    if (!m.min_native.empty() && mscl::ver_cmp(core, m.min_native) < 0) {
        err = "needs MSCore >= " + m.min_native + " (installed " + core + ")";
        return false;
    }

    bool enabled_txt = false;
    std::string dir = "Mods/" + m.name;  // zip-relative
    auto check = [&](const fs::path& stage, std::string& e) {
        fs::path mf = stage / mscl::to_wide(dir) / L"mscore.mod";
        std::error_code ec;
        if (!fs::is_regular_file(mf, ec)) {
            e = "zip has no " + dir + "/mscore.mod";
            return false;
        }
        std::string got = mscl::parse_kv(mscl::read_text(mf))["repo"];
        if (mscl::lower(got) != mscl::lower(repo)) {
            e = "mscore.mod repo '" + got + "' is not " + repo;
            return false;
        }
        enabled_txt = fs::is_regular_file(stage / mscl::to_wide(dir) / L"enabled.txt", ec);
        return true;
    };
    mscl::Mod mod;
    mod.name = m.name;
    mod.repo = repo;
    if (!mscl::stage_update(p, mod, m, err, check)) return false;
    if (mscl::apply_pending(p, log_line) == 0) {
        err = "apply failed (see MSCoreManager.log)";
        return false;
    }
    msg = "Added " + m.name + " " + m.version + ".";
    if (!enabled_txt) msg += " No enabled.txt in zip: add '" + m.name + " : 1' to mods.txt to load it.";
    return true;
}

int add_cli(const fs::path& win64, const std::string& arg) {
    if (game_running_under(win64)) {
        log_line("add: game is running - close it first");
        return 1;
    }
    std::string repo = mscl::parse_repo(arg), msg, err;
    if (repo.empty()) {
        log_line("add: FAILED repo must be owner/repo or a GitHub URL");
        return 1;
    }
    if (!add_mod_core(mscl::paths_from_win64(win64), repo, msg, err)) {
        log_line("add: FAILED " + err);
        return 1;
    }
    log_line("add: OK " + msg);
    return 0;
}

// ---------- remove mod ----------

// Game must be closed. Only listed files are deleted (see mscl::remove_mod).
int remove_cli(const fs::path& win64, const std::string& mod) {
    if (game_running_under(win64)) {
        log_line("remove: game is running - close it first");
        return 1;
    }
    std::string err;
    if (!mscl::remove_mod(mscl::paths_from_win64(win64), mod, err)) {
        log_line("remove: FAILED " + err);
        return 1;
    }
    log_line("remove: OK " + mod);
    return 0;
}

// ---------- background work ----------

void start_check() {
    if (!g_has_game || g_busy) return;
    g_busy = true;
    mscl::Paths p = g_paths;
    std::thread([p]() {
        auto* out = new std::vector<mscl::Check>();
        try {
            std::vector<mscl::Mod> mods = mscl::scan_mods(p);
            std::string core = "0.0.0";
            for (const auto& m : mods)
                if (m.name == "MSCore") core = m.version;
            for (const auto& m : mods) out->push_back(mscl::check_mod(m, core));
        } catch (const std::exception& e) {
            log_line(std::string("check: ") + e.what());
        }
        PostMessageW(g_hwnd, WM_APP_CHECKED, 0, reinterpret_cast<LPARAM>(out));
    }).detach();
}

// Runs job on a worker thread; result text goes to the status line.
void run_work(std::function<std::string()> job) {
    g_busy = true;
    set_status("Working...");
    std::thread([job]() {
        std::string msg;
        try {
            msg = job();
        } catch (const std::exception& e) {
            msg = std::string("error: ") + e.what();
        }
        log_line(msg);
        PostMessageW(g_hwnd, WM_APP_WORK, 0, reinterpret_cast<LPARAM>(new std::string(msg)));
    }).detach();
}

// ---------- UI ----------

void refresh_list() {
    ListView_DeleteAllItems(g_list);
    for (int i = 0; i < static_cast<int>(g_rows.size()); ++i) {
        const mscl::Check& c = g_rows[i];
        std::wstring cells[4] = {mscl::to_wide(c.mod.name) + (mscl::mod_enabled(g_paths, c.mod.name) ? L"" : L" (disabled)"),
                                 mscl::to_wide(c.mod.version),
                                 mscl::to_wide(c.m.version.empty() ? std::string("-") : c.m.version),
                                 mscl::to_wide(c.state)};
        LVITEMW it{};
        it.mask = LVIF_TEXT;
        it.iItem = i;
        it.pszText = cells[0].data();
        ListView_InsertItem(g_list, &it);
        for (int s = 1; s < 4; ++s) ListView_SetItemText(g_list, i, s, cells[s].data());
    }
}

void update_ui() {
    bool running = game_running();
    bool can = g_has_game && !g_busy;
    EnableWindow(g_btn_check, can);
    EnableWindow(g_btn_add, can && !running);
    EnableWindow(g_btn_upd_sel, can);
    EnableWindow(g_btn_upd_all, can);
    EnableWindow(g_btn_install, can && !running);
    EnableWindow(g_btn_rollback, can && !running);
    EnableWindow(g_btn_remove, can && !running);
    EnableWindow(g_btn_toggle, can && !running);
    EnableWindow(g_btn_browse, !g_busy);
    std::string t = g_has_game ? "Game: " + mscl::to_utf8(g_paths.win64.wstring())
                               : std::string("No game folder - click Browse game folder");
    if (running) t += "   |   Game is running - updates apply next launch";
    SetWindowTextW(g_game_lbl, mscl::to_wide(t).c_str());
}

void do_update(bool all) {
    if (g_busy) return;
    std::vector<mscl::Check> picks;
    if (all) {
        for (const auto& c : g_rows)
            if (c.state == "update") picks.push_back(c);
    } else {
        for (int i = ListView_GetNextItem(g_list, -1, LVNI_SELECTED); i >= 0;
             i = ListView_GetNextItem(g_list, i, LVNI_SELECTED))
            if (g_rows[i].state == "update") picks.push_back(g_rows[i]);
    }
    if (picks.empty()) {
        set_status("Nothing to update - select rows whose status is update");
        return;
    }
    mscl::Paths p = g_paths;
    run_work([p, picks]() -> std::string {
        std::string msg;
        int ok = 0;
        for (const auto& c : picks) {
            std::string err;
            if (mscl::stage_update(p, c.mod, c.m, err)) ++ok;
            else msg += c.mod.name + " " + err + ". ";
        }
        if (!game_running_under(p.win64)) {
            int n = mscl::apply_pending(p, log_line);
            msg += "Applied " + std::to_string(n) + ". ";
        } else {
            msg += "Staged " + std::to_string(ok) + "; game running, applies next launch. ";
        }
        return msg;
    });
}

void do_install() {
    if (g_busy || !g_has_game) return;
    mscl::Paths p = g_paths;
    run_work([p]() -> std::string {
        std::string err;
        if (install_core(p, err)) return "MSCore installed.";
        return "Install failed: " + err;
    });
}

void do_rollback() {
    if (g_busy) return;
    int i = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (i < 0) {
        set_status("Select a mod row, then Rollback");
        return;
    }
    if (game_running()) {
        set_status("Game is running - close it first");
        return;
    }
    std::string err, name = g_rows[i].mod.name;
    if (mscl::rollback(g_paths, name, err)) set_status("Rolled back " + name + ". Restart game to apply.");
    else set_status("Rollback failed: " + err);
    start_check();
}

void do_browse() {
    std::wstring picked;
    if (!pick_folder(picked)) return;
    if (set_game_root(fs::path(picked), true)) {
        set_status("Game set: " + mscl::to_utf8(g_paths.win64.wstring()));
        start_check();
    } else {
        set_status("Not a Main Sequence folder (needs Windows\\MainSequence\\Binaries\\Win64)");
    }
    update_ui();
}

INT_PTR CALLBACK add_dlg_proc(HWND h, UINT msg, WPARAM wp, LPARAM) {
    switch (msg) {
        case WM_INITDIALOG:
            return TRUE;
        case WM_COMMAND:
            if (LOWORD(wp) == IDOK) {
                wchar_t buf[256] = {};
                GetDlgItemTextW(h, IDC_REPO, buf, 256);
                std::string r = mscl::parse_repo(mscl::to_utf8(buf));
                if (r.empty()) {
                    MessageBoxW(h, L"Enter owner/repo or a GitHub URL (github.com/owner/repo)", L"Add mod", MB_ICONWARNING);
                    return TRUE;
                }
                g_add_repo = r;
                EndDialog(h, IDOK);
                return TRUE;
            }
            if (LOWORD(wp) == IDCANCEL) {
                EndDialog(h, IDCANCEL);
                return TRUE;
            }
            break;
    }
    return FALSE;
}

void do_add() {
    if (g_busy || !g_has_game) return;
    if (game_running()) {
        set_status("Game is running - close it first");
        return;
    }
    if (DialogBoxParamW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDD_ADDMOD), g_hwnd, add_dlg_proc, 0) != IDOK)
        return;
    mscl::Paths p = g_paths;
    std::string repo = g_add_repo;
    run_work([p, repo]() -> std::string {
        if (game_running_under(p.win64)) return "Game is running - close it first";
        std::string msg, err;
        if (add_mod_core(p, repo, msg, err)) return msg;
        return "Add " + repo + " failed: " + err;
    });
}

void do_remove() {
    if (g_busy || !g_has_game) return;
    int i = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (i < 0) {
        set_status("Select a mod row, then Remove mod");
        return;
    }
    if (game_running()) {
        set_status("Game is running - close it first");
        return;
    }
    std::string name = g_rows[i].mod.name;
    if (mscl::lower(name) == "mscore") {
        set_status("MSCore itself cannot be removed here");
        return;
    }
    mscl::Paths p = g_paths;
    std::vector<mscl::RemoveItem> items;
    std::string err;
    if (!mscl::remove_plan(p, name, items, err)) {
        set_status("Remove " + name + ": " + err);
        return;
    }
    std::string q = "Remove " + name + "?\n\nThis deletes " + std::to_string(items.size()) +
                    " file(s) installed by " + name +
                    ", its backup and its mods.txt line.\n"
                    "Your own files (MiningBench.ini, loader.ini, *.log, other mods) are kept.";
    if (MessageBoxW(g_hwnd, mscl::to_wide(q).c_str(), L"Remove mod", MB_YESNO | MB_ICONWARNING) != IDYES) return;
    run_work([p, name]() -> std::string {
        if (game_running_under(p.win64)) return "Game is running - close it first";
        std::string e;
        if (mscl::remove_mod(p, name, e)) return "Removed " + name + ".";
        return "Remove " + name + " failed: " + e;
    });
}

void do_toggle() {
    if (g_busy || !g_has_game) return;
    int i = ListView_GetNextItem(g_list, -1, LVNI_SELECTED);
    if (i < 0) {
        set_status("Select a mod row, then Disable/Enable");
        return;
    }
    if (game_running()) {
        set_status("Game is running - close it first");
        return;
    }
    std::string name = g_rows[i].mod.name;
    if (mscl::lower(name) == "mscore") {
        set_status("MSCore itself cannot be disabled here");
        return;
    }
    bool on = !mscl::mod_enabled(g_paths, name);
    std::string err;
    if (mscl::set_mod_enabled(g_paths, name, on, err))
        set_status(std::string(on ? "Enabled " : "Disabled ") + name + ". Restart game to apply.");
    else
        set_status("Toggle " + name + " failed: " + err);
    start_check();
    refresh_list();
}

void on_command(int id) {
    switch (id) {
        case ID_CHECK: start_check(); break;
        case ID_ADD: do_add(); break;
        case ID_REMOVE: do_remove(); break;
        case ID_TOGGLE: do_toggle(); break;
        case ID_UPD_SEL: do_update(false); break;
        case ID_UPD_ALL: do_update(true); break;
        case ID_INSTALL: do_install(); break;
        case ID_ROLLBACK: do_rollback(); break;
        case ID_BROWSE: do_browse(); break;
    }
}

HWND mk(HWND parent, const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, parent,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    return c;
}

void create_controls(HWND h) {
    g_game_lbl = mk(h, L"STATIC", L"", 0, 12, 10, 988, 20, ID_GAME);
    g_list = mk(h, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SHOWSELALWAYS | WS_BORDER | WS_TABSTOP, 12, 36, 988, 300, ID_LIST);
    ListView_SetExtendedListViewStyle(g_list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER);
    const wchar_t* cols[] = {L"Mod", L"Installed", L"Latest", L"Status"};
    const int widths[] = {170, 110, 110, 598};
    for (int i = 0; i < 4; ++i) {
        LVCOLUMNW c{};
        c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        c.pszText = const_cast<LPWSTR>(cols[i]);
        c.cx = widths[i];
        c.iSubItem = i;
        ListView_InsertColumn(g_list, i, &c);
    }
    g_btn_check = mk(h, L"BUTTON", L"Check", BS_PUSHBUTTON, 12, 346, 136, 30, ID_CHECK);
    g_btn_add = mk(h, L"BUTTON", L"Add mod...", BS_PUSHBUTTON, 154, 346, 136, 30, ID_ADD);
    g_btn_upd_sel = mk(h, L"BUTTON", L"Update selected", BS_PUSHBUTTON, 296, 346, 136, 30, ID_UPD_SEL);
    g_btn_upd_all = mk(h, L"BUTTON", L"Update all", BS_PUSHBUTTON, 438, 346, 136, 30, ID_UPD_ALL);
    g_btn_install = mk(h, L"BUTTON", L"Install/Repair MSCore", BS_PUSHBUTTON, 580, 346, 136, 30, ID_INSTALL);
    g_btn_rollback = mk(h, L"BUTTON", L"Rollback", BS_PUSHBUTTON, 722, 346, 136, 30, ID_ROLLBACK);
    g_btn_browse = mk(h, L"BUTTON", L"Browse game folder", BS_PUSHBUTTON, 864, 346, 136, 30, ID_BROWSE);
    g_btn_remove = mk(h, L"BUTTON", L"Remove mod", BS_PUSHBUTTON, 12, 384, 136, 30, ID_REMOVE);
    g_btn_toggle = mk(h, L"BUTTON", L"Disable/Enable", BS_PUSHBUTTON, 154, 384, 136, 30, ID_TOGGLE);
    g_status = mk(h, L"STATIC", L"", 0, 12, 426, 988, 20, ID_STATUS);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_CREATE:
            create_controls(hwnd);
            return 0;
        case WM_COMMAND:
            if (HIWORD(wp) == BN_CLICKED) on_command(LOWORD(wp));
            return 0;
        case WM_APP_CHECKED: {
            auto* v = reinterpret_cast<std::vector<mscl::Check>*>(lp);
            g_rows = std::move(*v);
            delete v;
            g_busy = false;
            refresh_list();
            set_status((g_last.empty() ? std::string() : g_last + " | ") + "Checked " +
                       std::to_string(g_rows.size()) + " mod(s).");
            g_last.clear();
            update_ui();
            return 0;
        }
        case WM_APP_WORK: {
            auto* s = reinterpret_cast<std::string*>(lp);
            g_last = *s;
            set_status(*s);
            delete s;
            g_busy = false;
            update_ui();
            start_check();
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}  // namespace

int WINAPI wWinMain(HINSTANCE inst, HINSTANCE, LPWSTR, int show) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX ic{sizeof ic, ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&ic);

    std::wstring game_arg, source_arg, add_arg, remove_arg;
    bool headless = false;
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; i < argc; ++i) {
        std::wstring a = argv[i];
        if (a == L"--install") headless = true;
        else if (a == L"--add" && i + 1 < argc) add_arg = argv[++i];
        else if (a == L"--remove" && i + 1 < argc) remove_arg = argv[++i];
        else if (a == L"--game" && i + 1 < argc) game_arg = argv[++i];
        else if (a == L"--source" && i + 1 < argc) source_arg = argv[++i];
    }
    LocalFree(argv);
    if (!add_arg.empty() || !remove_arg.empty()) headless = true;
    if (!source_arg.empty()) {
        mscl::g_allow_local = true;
        mscl::g_local_root = mscl::to_utf8(source_arg);
    }

    if (!game_arg.empty()) {
        if (fs::is_directory(game_arg)) apply_game(game_arg);
    } else {
        fs::path s = saved_game();
        if (!s.empty() && fs::is_directory(s)) apply_game(s);
        else if (fs::path d = detect_game(); !d.empty()) apply_game(d);
    }

    if (headless) {
        if (!g_has_game) {
            log_line("no game folder (use --game <Win64 dir>)");
            return 1;
        }
        if (!add_arg.empty()) return add_cli(g_paths.win64, mscl::to_utf8(add_arg));  // owner/repo or GitHub URL, parsed in add_cli
        if (!remove_arg.empty()) return remove_cli(g_paths.win64, mscl::to_utf8(remove_arg));
        return install_cli(g_paths.win64);
    }

    if (!g_has_game) {
        std::wstring picked;
        if (pick_folder(picked)) set_game_root(fs::path(picked), true);
    }
    if (g_has_game && !game_running()) mscl::apply_pending(g_paths, log_line);

    WNDCLASSEXW wc{sizeof wc};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.hIcon = LoadIconW(inst, MAKEINTRESOURCEW(1));
    wc.hIconSm = static_cast<HICON>(LoadImageW(inst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON),
                                               GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR));
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<INT_PTR>(COLOR_BTNFACE + 1));
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT rc{0, 0, 1012, 460};
    AdjustWindowRect(&rc, style, FALSE);
    g_hwnd = CreateWindowExW(0, kClass, L"MSCore Manager", style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
                             rc.bottom - rc.top, nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return 1;
    ShowWindow(g_hwnd, show);
    update_ui();
    start_check();

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    return 0;
}
