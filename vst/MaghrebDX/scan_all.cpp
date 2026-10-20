// Every cartridge on this machine through the reader, so "it does not import"
// is answered with a count rather than a guess.

#include "dx_sysex.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <filesystem>

int main() {
    const char* base = std::getenv("LOCALAPPDATA");
    if (!base) { printf("no LOCALAPPDATA\n"); return 1; }
    const std::string dir = std::string(base) + "\\MaghrebDX\\Cartridges";
    printf("folder: %s\n", dir.c_str());
    if (!std::filesystem::exists(dir)) { printf("  MISSING\n"); return 1; }

    int files = 0, okc = 0, bad = 0, voices = 0, named = 0;
    std::vector<std::string> failures;
    for (auto& e : std::filesystem::directory_iterator(dir)) {
        if (e.path().extension() != ".syx") continue;
        files++;
        std::vector<mdx::SysexVoice> v;
        const int n = mdx::loadSysex(e.path().string().c_str(), v);
        if (n == 32) {
            okc++; voices += n;
            for (auto& s : v) if (s.name[0]) named++;
        } else {
            bad++;
            failures.push_back(e.path().filename().string() + " -> " + std::to_string(n));
        }
    }
    printf("%d files, %d parsed as 32 voices, %d rejected, %d voices, %d named\n",
           files, okc, bad, voices, named);
    for (auto& f : failures) printf("  REJECTED %s\n", f.c_str());

    std::vector<mdx::SysexVoice> v;
    if (mdx::loadSysex((dir + "\\rom1a.syx").c_str(), v) == 32) {
        printf("rom1a voices 1-8: ");
        for (int i = 0; i < 8; i++) printf("[%s] ", v[i].name);
        printf("\n");
    }
    return 0;
}
