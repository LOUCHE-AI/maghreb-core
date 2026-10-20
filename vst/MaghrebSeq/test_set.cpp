// Sets on disk, measured.
//
// The claim being tested is not "it writes a file" but "a set written today
// still loads correctly after the parameter list changes". That is the whole
// reason the format is keyed by name, and it is the failure that would be
// silent: an index-keyed file loads fine and simply puts every value in the
// wrong place.

#include "seq_preset.h"

#include <cstdio>
#include <cmath>
#include <map>
#include <string>
#include <vector>
#include <algorithm>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

// stands in for the plug-in's parameter table
struct Table {
    std::vector<std::string> names;
    std::vector<float> values;
};

static bool writeSet(const char* path, const Table& t, const mseq::SetRefs& r) {
    std::vector<const char*> n;
    for (auto& s : t.names) n.push_back(s.c_str());
    return mseq::saveSet(path, n.data(), t.values.data(), (int)t.values.size(), r);
}

// load into a table by name, exactly as the wrapper does
static bool readSet(const char* path, Table& t, mseq::SetRefs& r) {
    std::map<std::string, int> byName;
    for (int i = 0; i < (int)t.names.size(); i++) byName[t.names[i]] = i;
    return mseq::loadSet(path,
        [&](const std::string& name, double v) {
            auto it = byName.find(name);
            if (it != byName.end()) t.values[it->second] = (float)v;
        }, r);
}

int main() {
    char msg[400];
    const char* P = "test_set.mseq";

    Table a;
    a.names = {"Swing", "Master", "Output", "Song", "Song Loop",
               "L1 Rhythm", "L1 Kit", "L1 Level", "L1 Pan", "L1 Tune",
               "Engine", "Tone", "Vel>FM"};
    a.values = {0.24f, 0.80f, -3.5f, 1.f, 0.f,
                17.f, 2.f, 0.90f, 0.44f, -2.f,
                1.f, 0.5f, 0.55f};

    mseq::SetRefs refs;
    refs.kitPath = "C:\\Users\\x\\AppData\\Local\\MaghrebFM\\Kits\\bendir warm 2.mkit";
    refs.lane.resize(4);
    refs.lane[1].cart = "C:\\Users\\x\\AppData\\Local\\MaghrebDX\\Cartridges\\rom1a.syx";
    refs.lane[1].voice = 6;
    refs.lane[3].cart = "C:\\Users\\x\\AppData\\Local\\MaghrebDX\\Cartridges\\vrc110b.syx";
    refs.lane[3].voice = 31;

    ok("a set writes", writeSet(P, a, refs));

    // ---- round trip ----
    {
        Table b = a;
        for (auto& v : b.values) v = -999.f;
        mseq::SetRefs r2;
        const bool loaded = readSet(P, b, r2);

        double worst = 0;
        std::string bad;
        for (size_t i = 0; i < a.values.size(); i++) {
            const double d = std::fabs(a.values[i] - b.values[i]);
            if (d > worst) { worst = d; bad = a.names[i]; }
        }
        snprintf(msg, sizeof msg, "worst %.2e on %s", worst, bad.c_str());
        ok("every parameter comes back", loaded && worst < 1e-5, msg);

        ok("the kit path comes back", r2.kitPath == refs.kitPath, r2.kitPath);
        const bool lanes = r2.lane.size() >= 4 &&
            r2.lane[1].voice == 6  && r2.lane[1].cart == refs.lane[1].cart &&
            r2.lane[3].voice == 31 && r2.lane[3].cart == refs.lane[3].cart &&
            r2.lane[0].voice == -1 && r2.lane[2].voice == -1;
        snprintf(msg, sizeof msg, "%d lanes, [1]=%d [3]=%d",
                 (int)r2.lane.size(), r2.lane.size() > 1 ? r2.lane[1].voice : -9,
                 r2.lane.size() > 3 ? r2.lane[3].voice : -9);
        ok("each lane's cartridge and voice come back", lanes, msg);
    }

    // ---- the point of the format ----
    //
    // Insert a parameter in the middle, drop one, rename one, and append two -
    // exactly what has happened to this plug-in twice. Every surviving name
    // must still land on its own value.
    {
        Table c;
        c.names = {"Swing", "Shuffle",                 // inserted
                   "Master", "Output", "Song", "Song Loop",
                   "L1 Rhythm", "L1 Kit", "L1 Level", "L1 Pan", "L1 Tune",
                   "Engine", "Tone", "Vel>FM",
                   "Glide", "Width"};                  // appended
        c.values.assign(c.names.size(), -999.f);
        c.values[1] = 0.33f;                            // Shuffle: untouched default
        c.values[14] = 0.11f; c.values[15] = 0.22f;

        mseq::SetRefs r3;
        const bool loaded = readSet(P, c, r3);

        std::map<std::string, float> want;
        for (size_t i = 0; i < a.names.size(); i++) want[a.names[i]] = a.values[i];

        bool allRight = true;
        std::string bad;
        for (size_t i = 0; i < c.names.size(); i++) {
            auto it = want.find(c.names[i]);
            if (it == want.end()) continue;             // new parameter
            if (std::fabs(c.values[i] - it->second) > 1e-5) {
                allRight = false;
                if (bad.empty()) bad = c.names[i];
            }
        }
        ok("an older set still lands on the right parameters after the list grows",
           loaded && allRight, bad.empty() ? "" : ("wrong: " + bad));

        const bool untouched = std::fabs(c.values[1] - 0.33f) < 1e-6 &&
                               std::fabs(c.values[14] - 0.11f) < 1e-6 &&
                               std::fabs(c.values[15] - 0.22f) < 1e-6;
        ok("and parameters the file never mentioned keep their value", untouched);
    }

    // ---- a name that vanished must not shift anything ----
    {
        Table d;
        d.names = {"Master", "Output"};                 // Swing retired
        d.values = {-999.f, -999.f};
        mseq::SetRefs r4;
        readSet(P, d, r4);
        snprintf(msg, sizeof msg, "Master %.3f, Output %.3f", d.values[0], d.values[1]);
        ok("a retired name is skipped rather than shifting the rest",
           std::fabs(d.values[0] - 0.80f) < 1e-5 && std::fabs(d.values[1] + 3.5f) < 1e-5, msg);
    }

    // ---- rejections ----
    {
        FILE* f = fopen("junk.mseq", "w");
        fprintf(f, "not a set at all\nP|Swing|0.9\n");
        fclose(f);
        Table e = a; mseq::SetRefs r5;
        ok("a file that is not a set is refused", !readSet("junk.mseq", e, r5));
        remove("junk.mseq");

        Table g = a; mseq::SetRefs r6;
        ok("a missing file is refused", !readSet("no_such_file.mseq", g, r6));
    }

    // ---- a path with spaces and separators survives ----
    {
        mseq::SetRefs r7;
        r7.kitPath = "C:\\Some Folder\\with spaces\\my kit (2).mkit";
        r7.lane.resize(2);
        r7.lane[0].cart = "D:\\carts\\a|b weird.syx";   // separator inside a path
        r7.lane[0].voice = 3;
        Table h = a;
        writeSet("paths.mseq", h, r7);
        Table i2 = a; mseq::SetRefs r8;
        readSet("paths.mseq", i2, r8);
        snprintf(msg, sizeof msg, "kit \"%s\"", r8.kitPath.c_str());
        ok("paths with spaces survive", r8.kitPath == r7.kitPath, msg);
        ok("and a path containing the separator survives",
           r8.lane.size() > 0 && r8.lane[0].cart == r7.lane[0].cart,
           r8.lane.empty() ? "" : r8.lane[0].cart);
        remove("paths.mseq");
    }

    remove(P);
    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
