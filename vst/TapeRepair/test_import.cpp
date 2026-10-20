// Bringing a file in: reading the encodings that turn up, converting the rate
// without wrecking it, and handing anything else to ffmpeg.

#include "audio_import.h"
#include "wav_io.h"

#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>
#include <algorithm>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& d = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, d.empty() ? "" : "  ", d.c_str());
    if (!c) fails++;
}
static constexpr double kPi = 3.14159265358979323846;

// Frequency and level of the strongest partial near `near`, by correlation.
static void measure(const std::vector<float>& x, double sr, double near,
                    double& hz, double& db) {
    double best = near, bestMag = -1;
    for (double f = near * 0.9; f <= near * 1.1; f += near * 0.0005) {
        double re = 0, im = 0;
        for (size_t i = 0; i < x.size(); i++) {
            const double w = 0.5 * (1 - std::cos(2 * kPi * i / (x.size() - 1)));
            const double a = 2 * kPi * f * (double)i / sr;
            re += x[i] * w * std::cos(a); im -= x[i] * w * std::sin(a);
        }
        const double m = re * re + im * im;
        if (m > bestMag) { bestMag = m; best = f; }
    }
    hz = best;
    db = 20 * std::log10(std::max(1e-12, 4.0 * std::sqrt(bestMag) / (double)x.size()));
}

static std::vector<std::vector<float>> sine(double hz, double sr, double sec, int ch = 2) {
    std::vector<std::vector<float>> v((size_t)ch, std::vector<float>((size_t)(sr * sec), 0.f));
    for (int c = 0; c < ch; c++)
        for (size_t i = 0; i < v[0].size(); i++)
            v[(size_t)c][i] = 0.5f * (float)std::sin(2 * kPi * hz * (double)i / sr);
    return v;
}

int main() {
    namespace fs = std::filesystem;
    char msg[256];
    const fs::path dir = fs::temp_directory_path() / "taperepair_import_test";
    std::error_code ec; fs::remove_all(dir, ec); fs::create_directories(dir, ec);

    // ---- WAV, written and read back ----
    {
        const std::string p = (dir / "a.wav").string();
        auto in = sine(1000, 48000, 1.0);
        ok("a WAV is written", mwav::writeWav24(p, in, 48000));

        std::vector<std::vector<float>> back;
        int rate = 0;
        ok("and read back", mwav::readWav(p, back, rate));
        snprintf(msg, sizeof msg, "%zu ch, %d Hz, %zu frames",
                 back.size(), rate, back.empty() ? 0 : back[0].size());
        ok("with its shape intact",
           back.size() == 2 && rate == 48000 && back[0].size() == in[0].size(), msg);

        double worst = 0;
        for (size_t i = 0; i < in[0].size(); i++)
            worst = std::max(worst, (double)std::fabs(in[0][i] - back[0][i]));
        snprintf(msg, sizeof msg, "largest error %.2e", worst);
        ok("and its samples to 24 bits", worst < 1e-5, msg);

        // both channels survive - the reason this reader exists at all
        auto stereo = sine(1000, 48000, 0.5, 2);
        for (auto& s : stereo[1]) s *= -1.f;                 // right is inverted
        const std::string sp = (dir / "stereo.wav").string();
        mwav::writeWav24(sp, stereo, 48000);
        std::vector<std::vector<float>> sback; int sr2 = 0;
        mwav::readWav(sp, sback, sr2);
        ok("channels are kept apart, not summed",
           sback.size() == 2 && sback[0][100] * sback[1][100] < 0);
    }

    // ---- importAudio, WAV path, no rate change ----
    {
        const std::string p = (dir / "b.wav").string();
        mwav::writeWav24(p, sine(1000, 48000, 1.0), 48000);
        std::vector<std::vector<float>> out;
        std::string err;
        ok("importAudio reads a WAV with no external help",
           maudio::importAudio(p, out, 48000, &err) && out.size() == 2, err);
    }

    // ---- rate conversion ----
    {
        // 1 kHz at 44.1k into a 48k session: same note, right length
        auto v = sine(1000, 44100, 1.0, 1);
        maudio::resample(v, 44100, 48000);
        snprintf(msg, sizeof msg, "%zu frames, wanted ~48000", v[0].size());
        ok("upsampling gives the right length", std::abs((long)v[0].size() - 48000L) < 4, msg);

        double hz = 0, db = 0;
        measure(v[0], 48000, 1000, hz, db);
        snprintf(msg, sizeof msg, "%.2f Hz at %.1f dB", hz, db);
        ok("and the same pitch, at the same level", std::fabs(hz - 1000.0) < 1.0 && db > -7.0, msg);

        // The real test: downsampling must not fold high content back down.
        // 18 kHz at 48k, taken to 32k, has nowhere legal to go - a linear
        // interpolator would drop it in at 14 kHz as an audible whistle.
        auto hi = sine(18000, 48000, 0.5, 1);
        maudio::resample(hi, 48000, 32000);
        double ahz = 0, adb = 0;
        measure(hi[0], 32000, 14000, ahz, adb);
        snprintf(msg, sizeof msg, "alias at 14 kHz is %.1f dB", adb);
        ok("downsampling does not fold high content back as an alias", adb < -60.0, msg);
    }

    // ---- compressed formats ----
    {
        if (!maudio::haveFfmpeg()) {
            printf("SKIP   ffmpeg not installed, mp3 path not exercised\n");
        } else {
            const std::string wav = (dir / "src.wav").string();
            const std::string mp3 = (dir / "src.mp3").string();
            mwav::writeWav24(wav, sine(1000, 44100, 1.0), 44100);
            const std::string cmd = "ffmpeg -v error -y -i '" + wav + "' -b:a 192k '" + mp3 + "'";
            ok("an mp3 to test with", std::system(cmd.c_str()) == 0 && fs::exists(mp3, ec));

            std::vector<std::vector<float>> out;
            std::string err;
            const bool got = maudio::importAudio(mp3, out, 48000, &err);
            ok("an mp3 is imported", got && !out.empty(), err);
            if (got) {
                snprintf(msg, sizeof msg, "%zu ch, %zu frames at 48k", out.size(), out[0].size());
                ok("resampled to the session rate on the way in",
                   std::abs((long)out[0].size() - 48000L) < 3000, msg);
                double hz = 0, db = 0;
                measure(out[0], 48000, 1000, hz, db);
                snprintf(msg, sizeof msg, "%.1f Hz", hz);
                ok("and it is still the same note", std::fabs(hz - 1000.0) < 5.0, msg);
            }
        }
    }

    // ---- refusals ----
    {
        std::vector<std::vector<float>> out;
        std::string err;
        ok("a missing file is refused with a reason",
           !maudio::importAudio((dir / "nope.wav").string(), out, 48000, &err) && !err.empty(), err);

        const std::string junk = (dir / "junk.wav").string();
        { FILE* h = fopen(junk.c_str(), "wb"); fputs("not a wav at all", h); fclose(h); }
        ok("a file that is not audio is refused, not read as noise",
           !maudio::importAudio(junk, out, 48000, &err), err);
    }

    fs::remove_all(dir, ec);
    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
