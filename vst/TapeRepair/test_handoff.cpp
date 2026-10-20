// TapeRepair writes it, TapeGrain reads it - the whole connection between
// the two, so it is checked rather than assumed. Reaches into TapeGrain's
// own loader on purpose: testing against a copy of it would prove nothing.
#include "wav_io.h"
#include "tapegrain_io.h"
#include "handoff.h"
#include <cstdio>
#include <cmath>
#include <vector>
#include <filesystem>
#include <chrono>
#include <thread>
int fails = 0;
void ok(const char* l, bool c, const std::string& d=""){ printf("%s  %s%s%s\n", c?"PASS ":"FAIL ", l, d.empty()?"":"  ", d.c_str()); if(!c) fails++; }
int main(){
    namespace fs = std::filesystem;
    const auto dir = fs::temp_directory_path()/"handoff_test";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);
    const std::string path = (dir/"repaired.wav").string();

    // stereo, deliberately different per channel
    std::vector<std::vector<float>> ch(2, std::vector<float>(48000, 0.f));
    for (size_t i = 0; i < ch[0].size(); i++) {
        ch[0][i] = 0.5f * (float)std::sin(2*M_PI*440.0*i/48000.0);
        ch[1][i] = 0.5f * (float)std::sin(2*M_PI*554.0*i/48000.0);
    }
    ok("TapeRepair writes a 24-bit stereo WAV", mwav::writeWav24(path, ch, 48000));

    const auto sz = fs::file_size(path, ec);
    ok("of the right size", sz == 44 + 48000*2*3, std::to_string(sz));

    // now read it the way TapeGrain does
    tg::WavData w = tg::loadWav(path);
    ok("TapeGrain loads it", w.ok, w.ok ? "" : "loadWav refused it");
    ok("at the right rate and length", w.sampleRate == 48000 && w.mono.size() == 48000,
       std::to_string(w.sampleRate) + " Hz, " + std::to_string(w.mono.size()) + " frames");
    ok("and it sees both channels (it sums to mono)", w.channels == 2, std::to_string(w.channels));

    // the mono sum should equal the average of what was written
    double worst = 0;
    for (size_t i = 0; i < w.mono.size(); i++)
        worst = std::max(worst, (double)std::fabs(w.mono[i] - 0.5f*(ch[0][i]+ch[1][i])));
    char m[64]; snprintf(m, sizeof m, "largest error %.2e", worst);
    ok("with the samples intact to 24 bits", worst < 1e-4, m);

    // clipping is clipped, not wrapped - the thing that would make a quiet
    // mistake into a loud click
    std::vector<std::vector<float>> hot(1, std::vector<float>(128, 2.0f));
    const std::string hotPath = (dir/"hot.wav").string();
    mwav::writeWav24(hotPath, hot, 48000);
    tg::WavData h = tg::loadWav(hotPath);
    float minv = 1.f; for (float v : h.mono) minv = std::min(minv, v);
    snprintf(m, sizeof m, "lowest sample %.3f", minv);
    ok("an overshoot clips rather than wrapping to the opposite rail", minv > 0.9f, m);

    // ---- and TapeGrain finds the right one without being told ----
    {
        const auto box = fs::temp_directory_path()/"handoff_pick";
        std::error_code e; fs::remove_all(box, e); fs::create_directories(box, e);

        std::vector<std::vector<float>> tiny(2, std::vector<float>(480, 0.1f));
        auto write = [&](const char* name) {
            const std::string p = (box/name).string();
            mwav::writeWav24(p, tiny, 48000);
            // file clocks are coarse: without this the four land on one instant
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            return p;
        };

        write("TapeRepair 2026-01-01 100000 (off tape).wav");
        write("Some other sample.wav");                  // not a handoff
        write("TapeRepair notes.txt");                   // not audio
        const std::string wanted = write("TapeRepair 2026-01-02 110000 (repaired).wav");

        const std::string found = mhand::newestIn(box.string());
        ok("the newest tape is the one picked up", found == wanted,
           found.empty() ? "found nothing" : mhand::nameOf(found));

        fs::remove_all(box, e);
        ok("and an empty folder is no tape rather than a wrong one",
           mhand::newestIn(box.string()).empty());
    }

    fs::remove_all(dir, ec);
    printf("\n%s\n", fails?"FAILURES":"all good");
    return fails?1:0;
}
