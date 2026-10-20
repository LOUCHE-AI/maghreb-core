// The repair maths, against signals whose right answer is known by
// construction. Framework-free on purpose: none of this needs a host, so none
// of it should need one to be checked.

#include "tape_restore.h"

#include <cstdio>
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

static int fails = 0;
static void ok(const char* label, bool cond, const std::string& detail = "") {
    printf("%s  %s%s%s\n", cond ? "PASS " : "FAIL ", label,
           detail.empty() ? "" : "  ", detail.c_str());
    if (!cond) fails++;
}

static constexpr double kPi = 3.14159265358979323846;

static trestore::Clip tone(double hz, double sec, double sr = 48000, float amp = 0.5f,
                           int channels = 2) {
    trestore::Clip c;
    c.sampleRate = sr;
    c.ch.assign((size_t)channels, std::vector<float>((size_t)(sec * sr), 0.f));
    for (int ci = 0; ci < channels; ci++)
        for (size_t i = 0; i < c.frames(); i++)
            c.ch[(size_t)ci][i] = amp * (float)std::sin(2.0 * kPi * hz * (double)i / sr);
    return c;
}

// Level of one frequency, in dB, by correlating against it.
static double levelAt(const trestore::Clip& c, double hz) {
    double re = 0, im = 0;
    const size_t n = c.frames();
    for (size_t i = 0; i < n; i++) {
        const double t = 2.0 * kPi * hz * (double)i / c.sampleRate;
        re += c.ch[0][i] * std::cos(t);
        im += c.ch[0][i] * std::sin(t);
    }
    return trestore::linearToDb(2.0 * std::hypot(re, im) / (double)n);
}

static double rmsDb(const trestore::Clip& c, double fromSec, double toSec) {
    const size_t a = (size_t)(fromSec * c.sampleRate);
    const size_t b = std::min(c.frames(), (size_t)(toSec * c.sampleRate));
    double sum = 0; size_t n = 0;
    for (size_t i = a; i < b; i++) { sum += (double)c.ch[0][i] * c.ch[0][i]; n++; }
    return trestore::linearToDb(n ? std::sqrt(sum / (double)n) : 0.0);
}

int main() {
    char msg[256];

    // ---------------- DC offset ----------------
    {
        trestore::Clip c = tone(440, 1.0);
        for (auto& v : c.ch) for (float& s : v) s += 0.1f;
        trestore::removeDcOffset(c);
        double mean = 0;
        for (float s : c.ch[0]) mean += s;
        mean /= (double)c.frames();
        snprintf(msg, sizeof msg, "residual mean %.2e", mean);
        ok("DC offset is removed", std::fabs(mean) < 1e-6, msg);
    }

    // ---------------- hum ----------------
    {
        trestore::Clip c = tone(1000, 3.0, 48000, 0.4f);
        trestore::Clip h = tone(50, 3.0, 48000, 0.2f);
        for (int ci = 0; ci < 2; ci++)
            for (size_t i = 0; i < c.frames(); i++) c.ch[(size_t)ci][i] += h.ch[(size_t)ci][i];

        const double humBefore = levelAt(c, 50), musicBefore = levelAt(c, 1000);
        trestore::removeHum(c, {50.0, 4, 30.0});
        const double humAfter = levelAt(c, 50), musicAfter = levelAt(c, 1000);

        snprintf(msg, sizeof msg, "50 Hz %.1f -> %.1f dB", humBefore, humAfter);
        ok("mains hum is notched out", humAfter < humBefore - 40.0, msg);
        snprintf(msg, sizeof msg, "1 kHz %.1f -> %.1f dB", musicBefore, musicAfter);
        ok("the music either side of it survives", std::fabs(musicAfter - musicBefore) < 1.0, msg);
    }

    // ---------------- hiss ----------------
    {
        trestore::Clip c;
        c.sampleRate = 48000;
        c.ch.assign(2, std::vector<float>(48000 * 6, 0.f));
        unsigned seed = 12345;
        auto rnd = [&seed]() {
            seed = seed * 1664525u + 1013904223u;
            return ((float)(seed >> 8) / 8388608.f) - 1.f;
        };
        for (int ci = 0; ci < 2; ci++)
            for (size_t i = 0; i < c.frames(); i++) {
                float v = 0.02f * rnd();
                if (i > (size_t)48000 * 2)
                    v += 0.4f * (float)std::sin(2.0 * kPi * 1000.0 * (double)i / 48000.0);
                c.ch[(size_t)ci][i] = v;
            }

        const trestore::NoiseProfile p = trestore::measureNoise(c, 0.2, 1.5);
        ok("a noise profile is measured from a quiet stretch", p.valid());

        // With nothing to subtract, overlap-add must hand the signal back
        // unchanged - the check that the reconstruction itself is sound.
        {
            trestore::Clip copy = c;
            trestore::NoiseProfile zero = p;
            for (float& m : zero.mag) m = 0.f;
            trestore::reduceHiss(copy, zero, {1.0f, -60.f});
            double worst = 0;
            for (size_t i = 4096; i + 4096 < c.frames(); i++)
                worst = std::max(worst, (double)std::fabs(copy.ch[0][i] - c.ch[0][i]));
            snprintf(msg, sizeof msg, "largest difference %.2e", worst);
            ok("overlap-add reconstructs exactly when nothing is subtracted", worst < 1e-4, msg);
        }

        const double hissBefore = rmsDb(c, 0.2, 1.8);
        const double toneBefore = levelAt(c, 1000);
        ok("hiss reduction runs", trestore::reduceHiss(c, p, {1.0f, -18.f}));
        const double hissAfter = rmsDb(c, 0.2, 1.8);
        const double toneAfter = levelAt(c, 1000);

        snprintf(msg, sizeof msg, "floor %.1f -> %.1f dB", hissBefore, hissAfter);
        ok("the hiss floor comes down", hissAfter < hissBefore - 5.0, msg);
        snprintf(msg, sizeof msg, "1 kHz %.1f -> %.1f dB", toneBefore, toneAfter);
        ok("the music is left alone", std::fabs(toneAfter - toneBefore) < 2.0, msg);
    }

    // ---------------- finding the quiet part by itself ----------------
    {
        trestore::Clip c;
        c.sampleRate = 48000;
        c.ch.assign(1, std::vector<float>(48000 * 8, 0.f));
        unsigned seed = 99;
        auto rnd = [&seed]() { seed = seed*1664525u+1013904223u; return ((float)(seed>>8)/8388608.f)-1.f; };
        // loud everywhere except a gap from 5.0 to 6.5 s
        for (size_t i = 0; i < c.frames(); i++) {
            const double t = (double)i / 48000.0;
            c.ch[0][i] = 0.02f * rnd();
            if (!(t > 5.0 && t < 6.5))
                c.ch[0][i] += 0.5f * (float)std::sin(2.0 * kPi * 300.0 * t);
        }
        const trestore::NoiseProfile q = trestore::measureQuietest(c, 1.0);
        ok("the quietest stretch is found without being told where", q.valid());

        // Compared in the bin the music actually occupies. Summing every bin
        // would weigh one loud tone against a thousand bins of broadband
        // noise and answer a different question than the one being asked.
        const trestore::NoiseProfile loud = trestore::measureNoise(c, 1.0, 1.0);
        const int bin = (int)std::lround(300.0 / (48000.0 / q.fftSize));
        const double ratio = trestore::linearToDb(loud.mag[(size_t)bin] / q.mag[(size_t)bin]);
        snprintf(msg, sizeof msg, "at 300 Hz the gap is %.1f dB below the music", ratio);
        ok("and it holds no music, only floor", ratio > 20.0, msg);
    }

    // ---------------- level ----------------
    {
        trestore::Clip c = tone(440, 0.5, 48000, 0.1f);
        trestore::normalize(c, -1.0);
        const double pk = trestore::linearToDb(trestore::peak(c));
        snprintf(msg, sizeof msg, "peak %.2f dB", pk);
        ok("normalise lands on the target peak", std::fabs(pk + 1.0) < 0.05, msg);
    }

    // ---------------- guards ----------------
    {
        trestore::Clip empty;
        trestore::removeDcOffset(empty);
        trestore::removeHum(empty);
        trestore::normalize(empty);
        ok("an empty clip is handled everywhere", !trestore::measureQuietest(empty).valid());

        trestore::Clip c = tone(440, 1.0, 48000);
        trestore::NoiseProfile wrong = trestore::measureNoise(c, 0.0, 1.0);
        wrong.sampleRate = 44100;
        ok("a profile from another sample rate is refused, not applied",
           !trestore::reduceHiss(c, wrong));
    }

    // ---- split across cores, same tape out ----
    //
    // The denoise is the only stage long enough to be worth threading, and the
    // only one where threading could change the answer: overlap-add is a sum,
    // and a sum in a different order is a different set of roundings. So what
    // is checked is not that the two agree bit for bit - they cannot - but
    // that they agree far below anything a 24-bit file could carry.
    {
        const double sr = 48000;
        trestore::Clip ref;
        ref.sampleRate = sr;
        const size_t n = (size_t)(sr * 12);
        ref.ch.assign(2, std::vector<float>(n, 0.f));
        unsigned seed = 3;
        auto rnd = [&seed]{ seed = seed*1664525u+1013904223u; return ((float)(seed>>8)/8388608.f)-1.f; };
        for (int ch = 0; ch < 2; ch++)
            for (size_t i = 0; i < n; i++)
                ref.ch[(size_t)ch][i] = 0.02f * rnd()
                    + 0.3f * (float)std::sin(2*kPi*220.0*(double)i/sr);

        const trestore::NoiseProfile prof = trestore::measureQuietest(ref, 1.0);

        trestore::Clip one = ref, many = ref;
        trestore::HissOptions a; a.threads = 1;
        trestore::HissOptions b; b.threads = 8;
        trestore::reduceHiss(one, prof, a);
        trestore::reduceHiss(many, prof, b);

        double worst = 0.0;
        for (int ch = 0; ch < 2; ch++)
            for (size_t i = 0; i < n; i++)
                worst = std::max(worst, (double)std::fabs(many.ch[(size_t)ch][i]
                                                        - one.ch[(size_t)ch][i]));
        char msg[120];
        const double db = worst > 0 ? 20.0 * std::log10(worst) : -999.0;
        snprintf(msg, sizeof msg, "worst difference %.1e (%.0f dB, and 24-bit stops at -144)",
                 worst, db);
        // -120 dB is already twenty below the quietest thing a 24-bit file holds.
        ok("eight threads and one give the same tape", worst < 1e-6, msg);
    }

    // ---- finding the songs on a side ----
    {
        const double sr = 48000;
        unsigned seed = 5;
        auto rnd = [&seed]{ seed = seed*1664525u+1013904223u; return ((float)(seed>>8)/8388608.f)-1.f; };

        std::vector<float> side;
        // hiss, not silence: a tape between songs is still running
        auto gap  = [&](double sec){ for (size_t i=0;i<(size_t)(sec*sr);i++) side.push_back(0.002f*rnd()); };
        auto song = [&](double sec){ for (size_t i=0;i<(size_t)(sec*sr);i++)
                                        side.push_back(0.3f*(float)std::sin(2*kPi*220.0*(double)i/sr)
                                                     + 0.002f*rnd()); };
        gap(3.0); song(30.0); gap(4.0); song(45.0); gap(4.0); song(25.0); gap(5.0);

        trestore::Clip c;
        c.sampleRate = sr;
        c.ch.assign(2, side);

        const std::vector<trestore::Track> t = trestore::findTracks(c);
        char msg[160];
        snprintf(msg, sizeof msg, "found %zu", t.size());
        ok("three songs with gaps between them come back as three tracks",
           t.size() == 3, msg);

        if (t.size() == 3) {
            snprintf(msg, sizeof msg, "%.1f, %.1f, %.1f s",
                     t[0].seconds(), t[1].seconds(), t[2].seconds());
            ok("each the length it was recorded at",
               std::fabs(t[0].seconds() - 30.0) < 0.5 &&
               std::fabs(t[1].seconds() - 45.0) < 0.5 &&
               std::fabs(t[2].seconds() - 25.0) < 0.5, msg);

            // the boundaries sit on the music, not in the middle of the gap
            snprintf(msg, sizeof msg, "first starts at %.2f s", t[0].startSec);
            ok("and starts where the music does, not where the tape does",
               std::fabs(t[0].startSec - 3.0) < 0.3, msg);

            const trestore::Clip one = trestore::extract(c, t[1]);
            snprintf(msg, sizeof msg, "%.1f s lifted out", one.seconds());
            ok("a track lifted out is that track", std::fabs(one.seconds() - 45.0) < 0.5, msg);
        }
    }

    // ---- a side with nothing to divide it ----
    {
        const double sr = 48000;
        trestore::Clip c = tone(220, 40.0, sr, 0.3f);
        c.ch.push_back(c.ch[0]);
        const std::vector<trestore::Track> t = trestore::findTracks(c);
        char msg[80];
        snprintf(msg, sizeof msg, "%zu track of %.0f s", t.size(), t.empty()?0.0:t[0].seconds());
        ok("one unbroken recording stays one track", t.size() == 1, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
