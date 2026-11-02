// msc_loader tests: plain asserts. Prints ALL OK, exit 1 on first failure.
#include "msc_loader.hpp"

#include <cstdio>
#include <cstdlib>
#include <random>

using namespace mscl;

static int g_checks = 0;
#define CHECK(x)                                                                 \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(x)) {                                                              \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x);     \
            std::exit(1);                                                        \
        }                                                                        \
    } while (0)

static const char* kVdf = R"V(
"libraryfolders"
{
	"0"
	{
		"path"		"F:\\SteamLibrary"
		"label"		""
	}
	"1"
	{
		"path"		"D:\\SteamLibrary"
		"label"		""
	}
}
)V";

// copy of ue4ss\Mods\mods.txt from the Main Sequence Demo install (MSCore : 1 on line 20)
static const char* kMods = R"M(KismetDebuggerMod : 1
EventViewerMod : 1
CheatManagerEnablerMod : 1
ActorDumperMod : 0
ConsoleCommandsMod : 1
ConsoleEnablerMod : 1
SplitScreenMod : 0
LineTraceMod : 1
BPML_GenericFunctions : 1
BPModLoaderMod : 1
jsbLuaProfilerMod : 0



; Built-in keybinds, do not move up!
Keybinds : 1
MiningBenchMod : 1
MiningUnitUI : 1
MSCore : 1
MSDevMenu : 1
)M";

static bool zip_ok(std::vector<std::string> v) {
    std::string e;
    return zip_entries_ok(v, e);
}

int main() {
    // ---- versions
    CHECK(ver_cmp("1.2.10", "1.2.9") == 1);
    CHECK(ver_cmp("1.0", "1.0.0") == 0);
    CHECK(ver_cmp("v0.5.0", "0.4.9") == 1);
    CHECK(ver_cmp("bad", "0.0.1") == -1);  // bad = 0.0.0

    // ---- kv
    KV kv = parse_kv("# c\n; c\n  name = Foo Bar  \nversion=1.0\n\nbad line\n");
    CHECK(kv.size() == 2);
    CHECK(kv["name"] == "Foo Bar");
    CHECK(kv["version"] == "1.0");
    Manifest bom_m;
    std::string bom_err;
    CHECK(parse_manifest("\xEF\xBB\xBF" "name=M\nversion=1.0.0\nzip=x.zip\nsha256=abc\n", bom_m, bom_err));
    CHECK(bom_m.name == "M");

    // ---- sha
    CHECK(sha256_mem("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");

    // ---- zip entry policy
    CHECK(!zip_ok({"../x"}));
    CHECK(!zip_ok({"C:/x"}));
    CHECK(!zip_ok({"Mods/a.exe"}));
    CHECK(!zip_ok({"Foo/a.dll"}));
    CHECK(zip_ok({"Mods/M/dlls/main.dll", "Paks/p.pak", "Mods/"}));

    // ---- vdf
    auto libs = vdf_paths(kVdf);
    CHECK(libs.size() == 2);
    CHECK(libs[0] == L"F:\\SteamLibrary");
    CHECK(libs[1] == L"D:\\SteamLibrary");

    // ---- mods.txt
    std::string mods = kMods;
    std::string once = mods_txt_insert_first(mods, "MSCore : 1");
    std::string without = mods;
    without.erase(without.find("MSCore : 1\n"), 11);
    CHECK(once == "MSCore : 1\n" + without);
    CHECK(mods_txt_insert_first(once, "MSCore : 1") == once);
    CHECK(mods_txt_insert_first("MSCore : 0\nA : 1\n", "MSCore : 1") == "MSCore : 1\nA : 1\n");
    CHECK(!valid_game_dir(fs::path(L"C:\\nope_msc_test")));

    // ---- end to end in TEMP (local source, no network)
    std::random_device rd;
    char nm[40];
    std::snprintf(nm, sizeof nm, "msc_test_%08x", rd());
    fs::path tmp = fs::temp_directory_path() / nm;
    fs::path win64 = tmp / L"game" / L"Windows" / L"MainSequence" / L"Binaries" / L"Win64";
    Paths P = paths_from_win64(win64);
    CHECK(P.paks == tmp / L"game" / L"Windows" / L"MainSequence" / L"Content" / L"Paks");
    CHECK(P.core == win64 / L"ue4ss" / L"Mods" / L"MSCore");

    fs::path rel = tmp / L"rel";
    write_text(rel / L"Mods" / L"M" / L"dlls" / L"main.dll", "NEW");
    write_text(rel / L"Paks" / L"p.pak", "PAK");
    write_text(P.mods / L"M" / L"dlls" / L"main.dll", "OLD");

    fs::path zip = rel / L"x.zip";
    std::string out, err;
    CHECK(run_tar({L"-a", L"-cf", zip.wstring(), L"-C", rel.wstring(), L"Mods", L"Paks"}, out, err));

    std::string sha;
    CHECK(sha256_file(zip, sha));
    Manifest m;
    m.name = "M";
    m.version = "1.1.0";
    m.zip = "x.zip";
    m.sha256 = sha;
    m.size = static_cast<long long>(fs::file_size(zip));
    m.min_native = "0.1.0";
    Mod mod;
    mod.name = "M";
    mod.repo = "owner/repo";
    mod.version = "1.0.0";

    g_allow_local = true;
    g_local_root = rel.string();

    write_text(rel / L"manifest.txt", std::string("name=M\nversion=1.1.0\nzip=x.zip\nsha256=") + sha +
                                          "\nsize=" + std::to_string(m.size) + "\nmin_mscore_native=0.1.0\n");
    CHECK(parse_manifest(read_text(rel / L"manifest.txt"), m, err));
    CHECK(check_mod(mod, "0.5.0").state == "update");
    CHECK(check_mod(mod, "0.0.1").state == "skipped:min_native");

    CHECK(stage_update(P, mod, m, err));
    CHECK(read_text(P.core / L"pending.txt") == "M|1.1.0\n");
    CHECK(read_text(P.core / L"updates" / L"M" / L"1.1.0" / L"Mods" / L"M" / L"dlls" / L"main.dll") == "NEW");
    CHECK(!fs::exists(P.core / L"tmp" / L"x.zip"));

    std::string before = read_text(P.core / L"pending.txt");
    Manifest bad = m;
    bad.sha256 = std::string(64, '0');
    CHECK(!stage_update(P, mod, bad, err));
    CHECK(err == "hash mismatch");
    CHECK(read_text(P.core / L"pending.txt") == before);

    // pending is still "M|1.1.0" here, so apply is what we test next
    int n = apply_pending(P, [](const std::string&) {});
    CHECK(n == 1);
    CHECK(read_text(P.mods / L"M" / L"dlls" / L"main.dll") == "NEW");
    CHECK(read_text(P.paks / L"p.pak") == "PAK");
    CHECK(read_text(P.core / L"backup" / L"M" / L"Mods" / L"M" / L"dlls" / L"main.dll") == "OLD");
    CHECK(!fs::exists(P.core / L"pending.txt"));
    CHECK(read_text(P.core / L"installed" / L"M.txt") == "1.1.0\nMods/M/dlls/main.dll\nPaks/p.pak\n");

    CHECK(rollback(P, "M", err));
    CHECK(read_text(P.mods / L"M" / L"dlls" / L"main.dll") == "OLD");

    // remove: listed files go, overwritten-by-update file restored from backup, user files stay
    write_text(P.mods / L"M" / L"dlls" / L"main.dll", "NEW");  // back to the installed 1.1.0 state
    write_text(P.mods / L"M" / L"x.log", "log");               // not listed: must stay
    write_text(P.paks / L"keep.pak", "keep");                  // not listed: must stay
    write_text(P.mods / L"Other" / L"main.dll", "other");      // unrelated mod: must stay
    write_text(P.core / L"loader.ini", "force_install.M=1\nforce_install.Other=1\n");
    write_text(P.mods / L"mods.txt", "# hdr\nM : 1\nOther : 1\n");
    CHECK(!remove_mod(P, "MSCore", err));
    CHECK(!remove_mod(P, "Evil", err));  // no install list
    CHECK(remove_mod(P, "M", err));
    CHECK(!fs::exists(P.mods / L"M" / L"dlls" / L"main.dll"));
    CHECK(!fs::exists(P.paks / L"p.pak"));
    CHECK(read_text(P.mods / L"M" / L"x.log") == "log");
    CHECK(read_text(P.paks / L"keep.pak") == "keep");
    CHECK(read_text(P.mods / L"Other" / L"main.dll") == "other");
    CHECK(!fs::exists(P.core / L"backup" / L"M"));
    CHECK(!fs::exists(P.core / L"installed" / L"M.txt"));
    CHECK(read_text(P.core / L"loader.ini") == "force_install.Other=1\n");
    CHECK(read_text(P.mods / L"mods.txt") == "# hdr\nOther : 1\n");

    // path traversal in an install list is ignored, not followed
    write_text(P.core / L"installed" / L"Evil.txt", "1.0.0\nMods/../../evil.txt\n");
    CHECK(!remove_mod(P, "Evil", err));
    CHECK(!fs::exists(P.win64 / L"evil.txt"));

    g_allow_local = false;
    g_local_root.clear();
    std::error_code ec;
    fs::remove_all(tmp, ec);

    // parse_repo: accept
    CHECK(parse_repo("xxalex3xx97/MiningBenchMod") == "xxalex3xx97/MiningBenchMod");
    CHECK(parse_repo("  owner/repo  ") == "owner/repo");
    CHECK(parse_repo("github.com/owner/repo") == "owner/repo");
    CHECK(parse_repo("www.github.com/owner/repo/") == "owner/repo");
    CHECK(parse_repo("https://github.com/owner/repo") == "owner/repo");
    CHECK(parse_repo("HTTPS://GitHub.com/owner/repo") == "owner/repo");
    CHECK(parse_repo("http://www.github.com/owner/repo.git") == "owner/repo");
    CHECK(parse_repo("https://github.com/owner/repo/") == "owner/repo");
    CHECK(parse_repo("https://github.com/owner/repo/releases") == "owner/repo");
    CHECK(parse_repo("https://github.com/owner/repo/releases/tag/v1.0.0") == "owner/repo");
    CHECK(parse_repo("https://github.com/owner/repo/tree/main/src") == "owner/repo");
    CHECK(parse_repo("https://github.com/owner/repo?tab=x") == "owner/repo");
    CHECK(parse_repo("https://github.com/owner/repo#readme") == "owner/repo");
    // parse_repo: reject
    CHECK(parse_repo("").empty());
    CHECK(parse_repo("https://github.com/").empty());
    CHECK(parse_repo("https://evil.com/a/b").empty());
    CHECK(parse_repo("https://github.com.evil.com/a/b").empty());
    CHECK(parse_repo("github.com/a").empty());
    CHECK(parse_repo("../x").empty());
    CHECK(parse_repo("a/..").empty());
    CHECK(parse_repo("a/b/c").empty());
    CHECK(parse_repo("bad repo").empty());
    CHECK(parse_repo("noslash").empty());
    CHECK(parse_repo("ftp://github.com/a/b").empty());
    CHECK(parse_repo("https://user@github.com/a/b").empty());
    CHECK(parse_repo("https://github.com:443/a/b").empty());
    CHECK(parse_repo("https://github.com/a/b/issues").empty());
    CHECK(parse_repo("https://github.com/a/b/releases/latest").empty());
    CHECK(parse_repo("https://github.com/a/..").empty());

    // manifest_url: GitHub form carries ?t=<digits> (CDN cache bust); local form unchanged
    g_allow_local = false;
    std::string mu = manifest_url("o/r");
    const std::string mu_pre = "https://github.com/o/r/releases/latest/download/manifest.txt?t=";
    CHECK(mu.compare(0, mu_pre.size(), mu_pre) == 0);
    CHECK(mu.size() > mu_pre.size());
    CHECK(mu.find_first_not_of("0123456789", mu_pre.size()) == std::string::npos);
    g_allow_local = true;
    g_local_root = "C:/msc_nope_dir";  // no o_r subfolder -> root
    CHECK(manifest_url("o/r") == "C:/msc_nope_dir/manifest.txt");
    g_allow_local = false;
    g_local_root.clear();

    // http layer keeps the query string in the request path (WinHTTP splits it into extra info)
    Url cu;
    CHECK(crack(L"https://github.com/o/r/releases/latest/download/manifest.txt?t=1234", cu));
    CHECK(cu.https && cu.host == L"github.com");
    CHECK(cu.path == L"/o/r/releases/latest/download/manifest.txt?t=1234");
    CHECK(crack(L"https://github.com/o/r/releases/download/v1/x.zip", cu) && cu.path == L"/o/r/releases/download/v1/x.zip");

    std::printf("ALL OK\n");
    return 0;
}
