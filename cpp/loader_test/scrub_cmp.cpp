// Read-only: runs the C++ save scrubber on <in> (never writes it) and compares the bytes with the python result <expected>.
// Usage: scrub_cmp <in_S.sav> <expected.sav> [skip-first-pair]   (build: see scripts\test-loader.ps1 notes)
#include "msc_savescrub.hpp"

#include <cstdio>

using namespace mscl;

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const std::string B = "/Game/Blueprints/BuildingBlocks/", M = "/Game/Blueprints/Machines/";
    auto P = [](const std::string& d, const std::string& n) { return d + n + "." + n + "_C"; };
    std::vector<SaveScrub> pairs = {  // same pairs and stand-ins as revert_stock.py PAIRS
        {P(B, "BPO_MinerModuleBlock"), P(B, "BPO_WorkstationBlock"), P(B, "BPO_LadderBlock")},
        {P(M, "BPO_MiningMod"), P(M, "BPO_Workbench"), P(M, "BPO_Ladder_01")},
        {P(B, "BPO_BlueprintUnit"), P(B, "BPO_CommandModule"), P(B, "BPO_Station_2x1")},
        {P(M, "BP_BPUnitStoreObj_01"), P(M, "BP_ContainerObject_2"), P(M, "BPO_SignWall")}};
    if (argc > 3 && std::string(argv[3]) == "builtin") pairs = builtin_save_scrub("MiningBenchMod");  // auto stand-ins, as the manager does
    else if (argc > 3) pairs.erase(pairs.begin());
    std::string in = read_bin(argv[1]), want = argv[2][0] == '-' ? std::string() : read_bin(argv[2]);
    ScrubOut o = scrub_bytes(in, pairs);
    std::printf("status %d why '%s' nodes %d table %d paths %d merged %d renamed %d removed %d out %zu want %zu\n", static_cast<int>(o.st),
                o.why.c_str(), o.rep.nodes, o.rep.table, o.rep.paths, o.rep.merged, o.rep.renamed, o.rep.removed, o.data.size(), want.size());
    if (argv[2][0] == '-') {  // no expected file: report only; optional 4th arg = write the result there
        if (o.st == ScrubSt::Ok && argc > 4) {
            std::ofstream f(argv[4], std::ios::binary);
            f.write(o.data.data(), static_cast<std::streamsize>(o.data.size()));
        }
        return o.st == ScrubSt::Refused;
    }
    std::printf(o.st == ScrubSt::Ok && o.data == want ? "BYTE-IDENTICAL\n" : "DIFFERENT\n");
    return o.st == ScrubSt::Ok && o.data == want ? 0 : 1;
}
