// msc_loader tests: plain asserts. Prints ALL OK, exit 1 on first failure.
#include "msc_loader.hpp"
#include "msc_savescrub.hpp"

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

// ---- synthetic minimal _S.sav (same layout as the real format, see msc_savescrub.hpp)
static std::string w32(uint32_t v) { return ss::pk(v); }
static const std::string kMod = "/Game/Blueprints/BuildingBlocks/BPO_ModBlock.BPO_ModBlock_C";
static const std::string kStk = "/Game/Blueprints/BuildingBlocks/BPO_StkBlock.BPO_StkBlock_C";   // same length
static const std::string kStkLong = "/Game/Blueprints/BuildingBlocks/BPO_StockBlockLonger.BPO_StockBlockLonger_C";
static const std::string kOth = "/Game/Blueprints/BuildingBlocks/BPO_Other.BPO_Other_C";
// table: 6 fixed classes, then stock(6) modded(7) other(8)
static std::string make_save(bool withStock, bool withMod, bool shipStr, bool stats, int nodes, bool stockLong = false) {
    const std::string& stk = stockLong ? kStkLong : kStk;
    std::vector<std::string> T = {"/Script/A.ResearchState", "/Script/A.FactionManager", "/Script/A.M1",
                                  "/Script/A.M2", "/Script/A.Statistics", "/Script/A.StarCluster"};
    if (withStock) T.push_back(stk);
    if (withMod) T.push_back(kMod);
    T.push_back(kOth);
    std::string b = w32(0) + w32(39) + w32(static_cast<uint32_t>(T.size()));
    for (auto& t : T) b += ss::fstr(t);
    for (uint32_t v : {1u, 1u, 5u, 2u, 3u, 4u, 5000u, 777u}) b += w32(v);
    b += w32(0xAAAAAAAAu) + w32(0xBBBBBBBBu);
    uint32_t mi = 6 + (withStock ? 1 : 0);
    uint32_t si = withStock ? 6 : 0;
    // node payload as in the real saves: [idx][id][0][..][..][..][0][0][0][-5][395][0][6][parent]; word before = anything
    auto node = [&](uint32_t idx, uint32_t id) {
        return w32(idx) + w32(id) + w32(0) + w32(262144) + w32(262144) + w32(65536) + w32(0) + w32(0) + w32(0) + w32(0xFFFFFFFBu) + w32(395) +
               w32(0) + w32(6) + w32(1) + w32(0xCCCCCCCCu);
    };
    for (int i = 0; i < nodes; ++i) b += w32(i == 0 ? 10u : 0u) + w32(0) + node(mi, 100 + i);  // first one is off the old signature
    b += w32(65536) + w32(0) + node(si, 900);
    if (stats) {
        b += w32(0xEEEEEEEEu) + w32(3);
        b += ss::fstr(kOth) + w32(9) + ss::fstr(withStock ? stk : kOth + "x") + w32(5) + ss::fstr(kMod) + w32(2);
        b += w32(0xEEEEEEEEu);
    }
    if (shipStr) b += w32(0x11112222u) + ss::fstr(kMod) + w32(0x33334444u);
    b += w32(0xFFFFFFF0u);
    return b;
}

static void scrub_tests() {
    std::vector<SaveScrub> pairs = {{kMod, kStk, ""}};
    // ok: nodes, table, stats merge, ship string
    std::string a = make_save(true, true, true, true, 3);
    ScrubOut o = scrub_bytes(a, pairs);
    CHECK(o.st == ScrubSt::Ok);
    CHECK(o.data.find(kMod) == std::string::npos);
    CHECK(o.rep.nodes == 3 && o.rep.table == 1 && o.rep.merged == 1 && o.rep.paths == 1);
    ss::Parsed PA, PC;
    std::string why;
    CHECK(ss::parse(a, PA, why) && ss::parse(o.data, PC, why));
    CHECK(PC.table.size() == PA.table.size() && PC.table[7] == ss::standins()[0] && PC.table[6] == kStk && PC.table[8] == kOth);
    CHECK(ss::usage(o.data, 7, PC).empty());
    CHECK(ss::usage(o.data, 6, PC).size() == ss::usage(a, 6, PA).size() + 3);
    auto fm = ss::find_flatmaps(o.data, kStk, PC.tab_end);
    CHECK(fm.size() == 1 && fm[0].N == 2);
    auto md = ss::mapdict(o.data, fm[0].p);
    CHECK(md.size() == 2 && md[0].first == kOth && md[0].second == 9 && md[1].first == kStk && md[1].second == 7);
    // size: stats entry removed, table slot renamed to the stand-in (other length)
    CHECK(o.data.size() == a.size() - (ss::fstr(kMod).size() + 4) + (ss::fstr(ss::standins()[0]).size() - ss::fstr(kMod).size()));
    // scrubbing again is a no-op (nothing modded left)
    CHECK(scrub_bytes(o.data, pairs).st == ScrubSt::Skip);

    // stock entry absent in the stats map: key renamed, N unchanged
    std::string a2 = make_save(true, true, false, true, 1);
    {
        size_t k = a2.find(ss::fstr(kStk), a2.find(w32(0xEEEEEEEEu)));
        CHECK(k != std::string::npos);
        std::string alt = kStk;
        alt.replace(alt.find("BPO_Stk"), 7, "BPO_Xyz");
        a2.replace(k, ss::fstr(kStk).size(), ss::fstr(alt));
        ScrubOut o2 = scrub_bytes(a2, pairs);
        CHECK(o2.st == ScrubSt::Ok && o2.rep.renamed == 1 && o2.rep.merged == 0);
        CHECK(o2.data.find(kMod) == std::string::npos);
    }

    // refusals: stock not in table; per-ship path with another length. Nothing is returned to write.
    CHECK(scrub_bytes(make_save(false, true, false, false, 1), pairs).st == ScrubSt::Refused);
    ScrubOut r = scrub_bytes(make_save(true, true, true, true, 1, true), {{kMod, kStkLong, ""}});
    CHECK(r.st == ScrubSt::Refused && r.data.empty() && r.why.find("another length") != std::string::npos);
    // save without the class: skipped, not written
    CHECK(scrub_bytes(make_save(true, false, false, false, 0), pairs).st == ScrubSt::Skip);
    // fail closed: a plausible reference to the modded slot that is not a known node shape leaves the save untouched
    {
        std::string sus = make_save(true, true, false, true, 1);
        sus += w32(0) + w32(7) + w32(777) + w32(0) + w32(1) + w32(2);  // [0][idx 7][id 777][0]: looks like a node, wrong payload
        ScrubOut rf = scrub_bytes(sus, pairs);
        CHECK(rf.st == ScrubSt::Refused && rf.data.empty() && rf.why.find("cannot revert safely") != std::string::npos);
        // the same shape with a repeated id is a repeated record: explained, not a refusal
        std::string rep2 = make_save(true, true, false, true, 1) + w32(0) + w32(7) + w32(777) + w32(0) + w32(1) + w32(2) + w32(0) + w32(7) + w32(777) + w32(0) + w32(1) + w32(2);
        CHECK(scrub_bytes(rep2, pairs).st == ScrubSt::Ok);
    }
    // the off-signature first node was found (3 nodes, the first preceded by 10)
    CHECK(o.rep.nodes == 3 && ss::usage(a, 7, PA).size() == 3);
    // garbage that holds the path is refused, not touched
    CHECK(scrub_bytes("junk" + kMod, pairs).st == ScrubSt::Refused);
    // declarations
    auto d = parse_save_scrub("name=X\nsave_scrub=" + kMod + ">" + kStk + "\nsave_scrub=bad\nsave_scrub=/a>/a\n#save_scrub=/x>/y\n");
    CHECK(d.size() == 1 && d[0].modded == kMod && d[0].stock == kStk);

    // files: backup, atomic write, other saves untouched, designs rewritten in place (same length, never moved), dry run writes nothing
    std::random_device rd;
    char nm[40];
    std::snprintf(nm, sizeof nm, "msc_scrub_%08x", rd());
    fs::path t = fs::temp_directory_path() / nm, sd = t / L"SaveGames", bk = t / L"save-backup";
    write_text(sd / L"A_S.sav", a);
    write_text(sd / L"A_M.sav", "meta");
    write_text(sd / L"B-Autosave_S.sav", make_save(true, false, false, false, 0));
    write_text(sd / L"B-Autosave_M.sav", "metaB");
    write_text(sd / L"C_S.sav", make_save(false, true, false, false, 1));  // refused: stock missing
    // GVAS-like blob: magic, a number, the class path as a whole FString (twice), tail
    std::string dsg = "GVAS" + w32(3) + ss::fstr("/x") + ss::fstr(kMod) + w32(42) + ss::fstr(kOth) + ss::fstr(kMod) + "tail";
    write_text(sd / L"Designs" / L"d1.sav", dsg);
    write_text(sd / L"Designs" / L"d2.sav", "GVAS nothing");
    write_text(sd / L"Designs" / L"d3.sav", "GVAS" + w32(1) + kMod + "!");  // path not a whole FString: refused, left alone
    write_text(sd / L"DesignsBackup" / L"old.sav", "older");               // not ours: stays
    write_text(sd / L"DesignsBackup" / L"d1.sav", dsg);                    // same name as a design in Designs: stays
    write_text(sd / L"DesignsBackup" / L"r1.sav", "GVAS" + w32(9) + ss::fstr(kMod));  // moved by an old Remove: comes back
    write_text(sd / L"DesignsBackup" / L"r2.sav", dsg);                    // byte copy of d1: stays
    std::string bB = read_text(sd / L"B-Autosave_S.sav"), bC = read_text(sd / L"C_S.sav");
    ScrubSummary dry = scrub_saves(sd, bk, pairs, true);
    CHECK(dry.scrubbed() == 1 && dry.refused() == 1 && dry.designs_scrubbed() == 1 && dry.designs_refused() == 1 && dry.backup_dir.empty());
    CHECK(read_text(sd / L"A_S.sav") == a && read_text(sd / L"Designs" / L"d1.sav") == dsg && !fs::exists(bk));
    ScrubSummary run = scrub_saves(sd, bk, pairs, false);
    CHECK(run.scrubbed() == 1 && run.refused() == 1 && run.designs_scrubbed() == 1 && run.designs_refused() == 1);
    CHECK(read_text(sd / L"A_S.sav") == o.data);
    CHECK(read_text(run.backup_dir / L"A_S.sav") == a && read_text(run.backup_dir / L"A_M.sav") == "meta" &&
          fs::exists(run.backup_dir / L"RESTORE.txt"));
    CHECK(read_text(sd / L"B-Autosave_S.sav") == bB && read_text(sd / L"C_S.sav") == bC);
    CHECK(!fs::exists(run.backup_dir / L"B-Autosave_S.sav") && !fs::exists(sd / L"A_S.sav.scrubtmp"));
    // design d1: same length, only the two modded strings replaced, original in the backup, d2/d3 untouched, nothing moved
    std::string d1 = read_text(sd / L"Designs" / L"d1.sav");
    CHECK(d1.size() == dsg.size() && d1.find(kMod) == std::string::npos);
    std::string want = dsg;
    for (size_t o2 = want.find(kMod); o2 != std::string::npos; o2 = want.find(kMod)) want.replace(o2, kMod.size(), kStk);
    CHECK(d1 == want);
    CHECK(read_text(run.backup_dir / L"Designs" / L"d1.sav") == dsg);
    CHECK(read_text(sd / L"Designs" / L"d2.sav") == "GVAS nothing" && read_text(sd / L"Designs" / L"d3.sav") == "GVAS" + w32(1) + kMod + "!");
    CHECK(read_text(sd / L"DesignsBackup" / L"old.sav") == "older" && !fs::exists(sd / L"Designs" / L"old.sav"));
    CHECK(!fs::exists(sd / L"Designs" / L"r1.sav"));  // restore was not asked for
    // restore: --scrub-designs style (r1 comes back scrubbed; d1-named and byte-copy designs stay in DesignsBackup)
    write_text(sd / L"Designs" / L"d1.sav", dsg);  // pretend d1 is unscrubbed again so r2 is a byte copy of it
    ScrubSummary rs = scrub_saves(sd, bk, pairs, false, kScrubDesigns | kRestoreDesigns);
    CHECK(read_text(sd / L"Designs" / L"r1.sav") == "GVAS" + w32(9) + ss::fstr(kStk) && !fs::exists(sd / L"DesignsBackup" / L"r1.sav"));
    CHECK(fs::exists(sd / L"DesignsBackup" / L"r2.sav") && fs::exists(sd / L"DesignsBackup" / L"d1.sav") && fs::exists(sd / L"DesignsBackup" / L"old.sav"));
    CHECK(read_text(rs.backup_dir / L"DesignsBackup" / L"r1.sav") == "GVAS" + w32(9) + ss::fstr(kMod));
    CHECK(read_text(sd / L"Designs" / L"d1.sav") == want);
    // design refusals: stock path of another length, bad magic, garbage-free skip
    CHECK(scrub_design_bytes(dsg, {{kMod, kStkLong, ""}}).st == ScrubSt::Refused);
    CHECK(scrub_design_bytes("XXXX" + ss::fstr(kMod), pairs).st == ScrubSt::Refused);
    CHECK(scrub_design_bytes("GVAS nothing", pairs).st == ScrubSt::Skip);
    // restore the originals from the first backup (fresh backup of the scrubbed files first), then scrub again
    RestoreResult rr = restore_saves(run.backup_dir, sd, bk, false);
    CHECK(rr.restored >= 3 && rr.failed == 0 && read_text(sd / L"A_S.sav") == a && read_text(sd / L"A_M.sav") == "meta");
    CHECK(read_text(rr.fresh_backup / L"A_S.sav") == o.data && fs::exists(rr.fresh_backup / L"RESTORE.txt"));
    CHECK(read_text(sd / L"Designs" / L"d1.sav") == dsg);
    CHECK(restore_saves(run.backup_dir / L"nope", sd, bk, false).failed == 1);
    CHECK(scrub_saves(sd, bk, pairs, false).scrubbed() == 1 && read_text(sd / L"A_S.sav") == o.data);
    CHECK(run.text().find("REFUSED C_S.sav") != std::string::npos);
    std::error_code ec;
    fs::remove_all(t, ec);
}

int main(int argc, char** argv) {
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

    // remove without install list: the mod folder is the list (mscore.mod, files; .log stays)
    write_text(P.mods / L"U" / L"mscore.mod", "name=U\n");
    write_text(P.mods / L"U" / L"dlls" / L"main.dll", "U");
    write_text(P.mods / L"U" / L"a.log", "log");
    CHECK(remove_mod(P, "U", err));
    CHECK(!fs::exists(P.mods / L"U" / L"dlls" / L"main.dll"));
    CHECK(read_text(P.mods / L"U" / L"a.log") == "log");
    CHECK(!remove_mod(P, "U", err));  // mscore.mod gone: not installed

    // toggle: enabled.txt + mods.txt line (CRLF kept)
    write_text(P.mods / L"T" / L"enabled.txt", "");
    write_text(P.mods / L"mods.txt", "T : 1\r\n");
    CHECK(mod_enabled(P, "T"));
    CHECK(set_mod_enabled(P, "T", false, err));
    CHECK(!fs::exists(P.mods / L"T" / L"enabled.txt"));
    CHECK(read_text(P.mods / L"mods.txt") == "T : 0\r\n");
    CHECK(!mod_enabled(P, "T"));
    CHECK(set_mod_enabled(P, "T", true, err));
    CHECK(fs::exists(P.mods / L"T" / L"enabled.txt"));
    CHECK(read_text(P.mods / L"mods.txt") == "T : 1\r\n");
    CHECK(mod_enabled(P, "T"));
    CHECK(!set_mod_enabled(P, "MSCore", true, err));
    write_text(P.mods / L"mods.txt", "# hdr\nOther : 1\n");

    // path traversal in an install list is ignored, not followed
    write_text(P.core / L"installed" / L"Evil.txt", "1.0.0\nMods/../../evil.txt\n");
    CHECK(!remove_mod(P, "Evil", err));
    CHECK(!fs::exists(P.win64 / L"evil.txt"));

    // ---- regression: Remove/Disable of MiningBenchMod (install list of the real game). Old Remove restored the
    // backup\ copies (previous version of the same files) over the deleted ones: MiningUnitUI, pak98, enabled.txt stayed.
    {
        const char* kList = "0.3.1\nMods/MiningBenchMod/dlls/main.dll\nMods/MiningBenchMod/enabled.txt\nMods/MiningBenchMod/mscore.mod\n"
                            "Mods/MiningUnitUI/Scripts/main.lua\nMods/MiningUnitUI/Scripts/wbp_loader.lua\nMods/MiningUnitUI/enabled.txt\n"
                            "Paks/pakchunk101-MSMiningModel_P.pak\nPaks/pakchunk101-MSMiningModel_P.ucas\nPaks/pakchunk101-MSMiningModel_P.utoc\n"
                            "Paks/pakchunk95-MSMiningMesh_P.pak\nPaks/pakchunk95-MSMiningMesh_P.ucas\nPaks/pakchunk95-MSMiningMesh_P.utoc\n"
                            "Paks/pakchunk98-MiningUnitUI_P.pak\nPaks/pakchunk98-MiningUnitUI_P.ucas\nPaks/pakchunk98-MiningUnitUI_P.utoc\n"
                            "Paks/pakchunk99-ClonedBlock_P.pak\nPaks/pakchunk99-ClonedBlock_P.ucas\nPaks/pakchunk99-ClonedBlock_P.utoc\n";
        std::vector<std::string> rels;
        {
            std::istringstream in(kList);
            std::string l;
            std::getline(in, l);
            while (std::getline(in, l)) rels.push_back(l);
        }
        CHECK(rels.size() == 18);
        auto build = [&]() {
            std::error_code e2;
            fs::remove_all(P.mods / L"MiningBenchMod", e2);
            fs::remove_all(P.mods / L"MiningUnitUI", e2);
            fs::remove_all(P.core / L"backup", e2);
            fs::remove_all(P.core / L"disabled", e2);
            for (const auto& r : rels) write_text(target_for(P, r), "NEW " + r);
            // files an earlier update replaced were moved to backup\ (previous versions)
            for (const char* r : {"Mods/MiningBenchMod/enabled.txt", "Mods/MiningUnitUI/enabled.txt", "Mods/MiningUnitUI/Scripts/main.lua",
                                  "Mods/MiningUnitUI/Scripts/wbp_loader.lua", "Paks/pakchunk98-MiningUnitUI_P.pak", "Paks/pakchunk98-MiningUnitUI_P.ucas",
                                  "Paks/pakchunk98-MiningUnitUI_P.utoc"})
                write_text(P.core / L"backup" / L"MiningBenchMod" / to_wide(r), std::string("OLD ") + r);
            write_text(P.core / L"installed" / L"MiningBenchMod.txt", kList);
            write_text(P.mods / L"mods.txt", "# hdr\nMiningBenchMod : 1\nMiningUnitUI : 1\nOther : 1\n");
            write_text(P.mods / L"Other" / L"main.dll", "other");
            write_text(P.mods / L"MiningBenchMod" / L"MiningBench.ini", "ini");  // user file
        };
        auto none_left = [&]() {
            for (const auto& r : rels) CHECK(!fs::exists(target_for(P, r)));
        };

        build();
        CHECK(remove_mod(P, "MiningBenchMod", err));
        none_left();
        CHECK(!fs::exists(P.mods / L"MiningUnitUI"));  // no empty mod folder left
        CHECK(read_text(P.mods / L"MiningBenchMod" / L"MiningBench.ini") == "ini");  // only the user file keeps its folder
        CHECK(!fs::exists(P.core / L"backup" / L"MiningBenchMod"));
        CHECK(!fs::exists(P.core / L"installed" / L"MiningBenchMod.txt"));
        CHECK(read_text(P.mods / L"mods.txt") == "# hdr\nOther : 1\n");
        CHECK(read_text(P.mods / L"Other" / L"main.dll") == "other");

        // disable: nothing loadable stays in Mods\ / Paks\, all in disabled\; enable puts it all back
        build();
        fs::remove(P.mods / L"MiningBenchMod" / L"MiningBench.ini");
        CHECK(set_mod_enabled(P, "MiningBenchMod", false, err));
        none_left();
        CHECK(!fs::exists(P.mods / L"MiningBenchMod") && !fs::exists(P.mods / L"MiningUnitUI"));
        for (const auto& r : rels) CHECK(read_text(P.core / L"disabled" / L"MiningBenchMod" / to_wide(r)) == "NEW " + r);
        CHECK(!mod_enabled(P, "MiningBenchMod"));
        CHECK(read_text(P.mods / L"mods.txt") == "# hdr\nMiningBenchMod : 0\nMiningUnitUI : 0\nOther : 1\n");
        bool listed = false;  // still listed in the manager so it can be enabled again
        for (const auto& m : scan_mods(P)) listed = listed || m.name == "MiningBenchMod";
        CHECK(listed);
        CHECK(set_mod_enabled(P, "MiningBenchMod", true, err));
        for (const auto& r : rels)
            if (r != "Mods/MiningBenchMod/enabled.txt") CHECK(read_text(target_for(P, r)) == "NEW " + r);
        CHECK(fs::exists(P.mods / L"MiningBenchMod" / L"enabled.txt") && fs::exists(P.mods / L"MiningUnitUI" / L"enabled.txt"));
        CHECK(!fs::exists(P.core / L"disabled" / L"MiningBenchMod"));
        CHECK(mod_enabled(P, "MiningBenchMod"));
        CHECK(read_text(P.mods / L"mods.txt") == "# hdr\nMiningBenchMod : 1\nMiningUnitUI : 1\nOther : 1\n");

        // remove while disabled leaves nothing either
        CHECK(set_mod_enabled(P, "MiningBenchMod", false, err));
        CHECK(remove_mod(P, "MiningBenchMod", err));
        none_left();
        CHECK(!fs::exists(P.core / L"disabled" / L"MiningBenchMod"));
        CHECK(read_text(P.mods / L"mods.txt") == "# hdr\nOther : 1\n");
        fs::remove_all(P.mods / L"MiningBenchMod");
        fs::remove_all(P.core / L"installed" / L"MiningBenchMod.txt");
    }

    scrub_tests();

    // version constants the manager shows
    CHECK(std::string(kVersion) == "0.8.15" && kNativeApi >= 16);

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

    // ---- requires: parse_deps
    std::vector<Dep> deps;
    std::string derr;
    CHECK(parse_deps("", deps, derr) && deps.empty());
    CHECK(parse_deps("owner/a@1.2.3, Owner2/b-c@0.1.0", deps, derr));
    CHECK(deps.size() == 2 && deps[0].repo == "owner/a" && deps[0].min_ver == "1.2.3");
    CHECK(deps[1].repo == "Owner2/b-c" && deps[1].min_ver == "0.1.0");
    for (const char* bad : {"owner/a", "owner/a@1.2", "owner/a@v1.2.3", "owner/a@x.y.z", "../x@1.0.0", "owner/a@1.0.0,", "a/..@1.0.0"}) {
        CHECK(!parse_deps(bad, deps, derr));
        CHECK(deps.empty());
    }

    // ---- requires: parse_manifest (unknown keys are ignored by parse_kv, bad requires is an error)
    Manifest rq;
    std::string rerr;
    const std::string mhead = "name=P\nversion=1.0.0\nzip=p.zip\nsha256=abc\n";
    CHECK(parse_manifest(mhead + "requires=owner/a@1.2.3,owner/b@0.1.0\n", rq, rerr) && rq.requires_.size() == 2);
    CHECK(parse_manifest(mhead, rq, rerr) && rq.requires_.empty());
    CHECK(!parse_manifest(mhead + "requires=owner/a\n", rq, rerr) && rerr.find("requires") != std::string::npos);

    // ---- requires: scan_mods, dep_ok, dependents_of (installed mods in a temp game)
    fs::path dt = fs::temp_directory_path() / (std::string(nm) + "_dep");
    Paths DP = paths_from_win64(dt / L"game" / L"Windows" / L"MainSequence" / L"Binaries" / L"Win64");
    write_text(DP.mods / L"Dep" / L"mscore.mod", "name=Dep\nrepo=Owner/Dep\nversion=1.0.0\n");
    write_text(DP.mods / L"X" / L"mscore.mod", "name=X\nrepo=o/x\nversion=2.0.0\nrequires=owner/dep@1.0.0\n");
    std::vector<Mod> sm = scan_mods(DP);
    CHECK(sm.size() == 2 && sm[1].name == "X" && sm[1].requires_.size() == 1 && sm[1].requires_[0].repo == "owner/dep");
    std::string why;
    CHECK(dep_ok(DP, Dep{"owner/dep", "1.0.0"}, why));  // repo is case-insensitive
    CHECK(!dep_ok(DP, Dep{"owner/dep", "1.0.1"}, why) && why == "owner/dep 1.0.0 < 1.0.1");
    CHECK(!dep_ok(DP, Dep{"owner/none", "1.0.0"}, why) && why == "owner/none not installed");
    CHECK(dependents_of(DP, "Dep") == std::vector<std::string>{"X"});
    CHECK(dependents_of(DP, "X").empty());
    CHECK(dependents_of(DP, "Nope").empty());

    // check_mod: installed mod whose dependency is missing -> needs:<repo>; without Paths the state is unchanged
    write_text(DP.mods / L"Y" / L"mscore.mod", "name=Y\nrepo=o/x\nversion=1.0.0\nrequires=owner/missing@1.0.0\n");
    fs::path dsrc = dt / L"src";
    write_text(dsrc / L"manifest.txt", "name=X\nversion=2.0.0\nzip=x.zip\nsha256=abc\n");
    g_allow_local = true;
    g_local_root = dsrc.string();
    Mod ym;
    for (const Mod& m : scan_mods(DP))
        if (m.name == "Y") ym = m;
    CHECK(ym.name == "Y");
    CHECK(check_mod(ym, "0.5.0", &DP).state == "needs:owner/missing");
    CHECK(check_mod(ym, "0.5.0").state == "update");
    g_allow_local = false;
    g_local_root.clear();
    std::error_code dec;
    fs::remove_all(dt, dec);

    // ---- requires: end to end through MSCoreManager.exe --source (argv[1] = built manager)
    if (argc < 2) {
        std::fprintf(stderr, "usage: msc_loader_test <MSCoreManager.exe>\n");
        return 1;
    }
    fs::path mgr = argv[1];
    fs::path e2e = fs::temp_directory_path() / (std::string(nm) + "_e2e");
    fs::path src = e2e / L"src";
    fs::path game = e2e / L"game";
    fs::path gwin = game / L"Windows" / L"MainSequence" / L"Binaries" / L"Win64";
    fs::create_directories(game / L"Windows" / L"MainSequence" / L"Content" / L"Paks");
    fs::create_directories(gwin);
    Paths EP = paths_from_win64(gwin);

    // mod_req: requires= written into the zip's mscore.mod; man_req: requires= in manifest.txt
    auto publish = [&](const std::string& repo, const std::string& name, const std::string& ver,
                       const std::string& mod_req, const std::string& man_req) {
        std::string d = repo;
        std::replace(d.begin(), d.end(), '/', '_');
        fs::path out = src / to_wide(d);
        fs::create_directories(out);
        fs::path stg = src / L"stg";
        std::string kv = "name=" + name + "\nrepo=" + repo + "\nversion=" + ver + "\n";
        if (!mod_req.empty()) kv += "requires=" + mod_req + "\n";
        write_text(stg / L"Mods" / to_wide(name) / L"mscore.mod", kv);
        write_text(stg / L"Mods" / to_wide(name) / L"dlls" / L"main.dll", name);
        std::string o, pe;
        CHECK(run_tar({L"-a", L"-cf", (out / L"p.zip").wstring(), L"-C", stg.wstring(), L"Mods"}, o, pe));
        std::string sha;
        CHECK(sha256_file(out / L"p.zip", sha));
        std::string man = "name=" + name + "\nversion=" + ver + "\nzip=p.zip\nsha256=" + sha + "\nsize=" +
                          std::to_string(fs::file_size(out / L"p.zip")) + "\n";
        if (!man_req.empty()) man += "requires=" + man_req + "\n";
        write_text(out / L"manifest.txt", man);
        std::error_code ec;
        fs::remove_all(stg, ec);
    };
    auto run_mgr = [&](const std::string& args) {
        std::string cmd = "\"\"" + mgr.string() + "\" --game \"" + gwin.string() + "\" --source \"" + src.string() +
                          "\" " + args + "\"";
        return std::system(cmd.c_str());
    };

    publish("owner/dep", "Dep", "1.0.0", "", "");
    publish("owner/parent", "Parent", "1.0.0", "owner/dep@1.0.0", "owner/dep@1.0.0");
    publish("owner/orphan", "Orphan", "1.0.0", "owner/missing@1.0.0", "owner/missing@1.0.0");  // dep has no source
    publish("owner/bad", "Bad", "1.0.0", "owner/dep@1.0.0", "");                  // zip vs manifest requires differ
    publish("owner/cyc_a", "CycA", "1.0.0", "owner/cyc_b@1.0.0", "owner/cyc_b@1.0.0");  // cycle: depth guard
    publish("owner/cyc_b", "CycB", "1.0.0", "owner/cyc_a@1.0.0", "owner/cyc_a@1.0.0");
    publish("owner/upd", "Upd", "1.0.0", "", "");

    CHECK(run_mgr("--add owner/parent") == 0);
    CHECK(fs::exists(EP.mods / L"Parent" / L"mscore.mod"));
    CHECK(fs::exists(EP.mods / L"Dep" / L"mscore.mod"));  // dependency installed first
    CHECK(run_mgr("--add owner/orphan") != 0);
    CHECK(!fs::exists(EP.mods / L"Orphan" / L"mscore.mod"));  // parent not staged when a dep fails
    CHECK(run_mgr("--add owner/bad") != 0);
    CHECK(!fs::exists(EP.mods / L"Bad" / L"mscore.mod"));
    CHECK(run_mgr("--add owner/cyc_a") != 0);
    CHECK(!fs::exists(EP.mods / L"CycA" / L"mscore.mod"));
    CHECK(run_mgr("--remove Dep") == 0);  // a required mod can still be removed (warn only)
    CHECK(!fs::exists(EP.mods / L"Dep" / L"mscore.mod"));
    CHECK(fs::exists(EP.mods / L"Parent" / L"mscore.mod"));

    // update: Upd installed without its dep; release 1.1.0 requires it -> --update installs the dep, then 1.1.0
    CHECK(run_mgr("--add owner/upd") == 0);
    publish("owner/upd", "Upd", "1.1.0", "owner/dep@1.0.0", "owner/dep@1.0.0");
    CHECK(run_mgr("--update Upd") == 0);
    CHECK(fs::exists(EP.mods / L"Dep" / L"mscore.mod"));
    CHECK(read_text(EP.mods / L"Upd" / L"mscore.mod").find("version=1.1.0") != std::string::npos);
    // needs: Upd is 1.1.0 (up to date) but its dep was removed -> --update reinstalls the dep only
    CHECK(run_mgr("--remove Dep") == 0);
    CHECK(run_mgr("--update Upd") == 0);
    CHECK(fs::exists(EP.mods / L"Dep" / L"mscore.mod"));
    CHECK(read_text(EP.mods / L"Upd" / L"mscore.mod").find("version=1.1.0") != std::string::npos);
    // save scrub through the real manager: --scrub-saves --dry-run changes nothing, --remove backs up and scrubs
    {
        fs::path saves = e2e / L"saves";
        std::string sv = make_save(true, true, true, true, 2);
        write_text(saves / L"S1_S.sav", sv);
        write_text(saves / L"S1_M.sav", "meta");
        write_text(EP.mods / L"SMod" / L"mscore.mod", "name=SMod\nversion=1.0.0\nsave_scrub=" + kMod + ">" + kStk + "\n");
        write_text(EP.mods / L"SMod" / L"dlls" / L"main.dll", "x");
        std::string sa = " --saves \"" + saves.string() + "\"";
        CHECK(run_mgr("--scrub-saves SMod --dry-run" + sa) == 0);
        CHECK(read_text(saves / L"S1_S.sav") == sv && !fs::exists(EP.core / L"save-backup"));
        CHECK(run_mgr("--remove SMod" + sa) == 0);
        CHECK(!fs::exists(EP.mods / L"SMod"));
        CHECK(read_text(saves / L"S1_S.sav") == scrub_bytes(sv, {{kMod, kStk, ""}}).data);
        CHECK(read_text(saves / L"S1_S.sav").find(kMod) == std::string::npos);
        bool bkok = false;
        for (auto& e : fs::directory_iterator(EP.core / L"save-backup")) bkok = bkok || read_text(e.path() / L"S1_S.sav") == sv;
        CHECK(bkok);
    }
    fs::remove_all(e2e, dec);

    std::printf("ALL OK\n");
    return 0;
}
