// Recording, repairing and playing back a take. The maths is checked in
// test_restore.cpp; this is about the machine around it behaving.

#include "taperepair_dsp.h"
#include <chrono>
#include <cstdio>
#include <cmath>
#include <string>
#include <thread>
#include <vector>
#include <algorithm>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& d = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, d.empty() ? "" : "  ", d.c_str());
    if (!c) fails++;
}
static constexpr double kPi = 3.14159265358979323846;
static const double SR = 48000;

// A tape transfer: music, over hiss, with mains hum and a DC offset, and a
// gap early on with nothing but the floor in it.
static std::vector<float> transfer(size_t n) {
    std::vector<float> x(n);
    unsigned seed = 7;
    auto rnd = [&seed]{ seed = seed*1664525u+1013904223u; return ((float)(seed>>8)/8388608.f)-1.f; };
    for (size_t i = 0; i < n; i++) {
        const double t = (double)i / SR;
        float v = 0.02f * rnd();                                  // hiss
        v += 0.03f * (float)std::sin(2*kPi*50.0*t);               // hum
        v += 0.02f;                                               // dc
        if (t > 2.0) v += 0.35f * (float)std::sin(2*kPi*440.0*t); // music, after a gap
        x[i] = v;
    }
    return x;
}

static void feed(trep::TapeRepair& d, const std::vector<float>& in, int block = 128) {
    std::vector<float> oL(block), oR(block);
    for (size_t i = 0; i + block <= in.size(); i += block)
        d.processStereo(&in[i], &in[i], oL.data(), oR.data(), block);
}

int main() {
    char msg[256];
    const std::vector<float> tape = transfer((size_t)(SR * 6));

    trep::TapeRepair d;
    d.setSampleRate(SR);

    ok("nothing recorded to begin with", !d.hasTake() && !d.hasRepair());
    ok("play does nothing without a take", (d.play(true), !d.playing()));

    // ---- record ----
    d.arm(true);
    feed(d, tape);
    d.arm(false);
    snprintf(msg, sizeof msg, "%.2f s", d.takeSeconds());
    ok("arming records what goes in", d.hasTake() && std::fabs(d.takeSeconds() - 6.0) < 0.05, msg);

    // ---- learn ----
    ok("the noise floor is found without being told where", d.learn());
    ok("and it is remembered", d.hasProfile());

    // ---- repair ----
    ok("repair runs", d.repair());
    ok("and leaves the original take alone",
       d.hasRepair() && d.take().ch[0].size() == d.repaired().ch[0].size());

    const double moved = d.floorChangeDb();
    snprintf(msg, sizeof msg, "noise floor %+.1f dB", moved);
    ok("the noise floor comes down", moved < -5.0, msg);

    // dc offset is gone from the repair but still in the tape
    auto meanOf = [](const std::vector<float>& v) {
        double m = 0; for (float s : v) m += s; return m / (double)v.size(); };
    snprintf(msg, sizeof msg, "tape %+.4f, repair %+.4f",
             meanOf(d.take().ch[0]), meanOf(d.repaired().ch[0]));
    ok("the DC offset is removed from the repair, not from the tape",
       std::fabs(meanOf(d.repaired().ch[0])) < 1e-4 && meanOf(d.take().ch[0]) > 0.01, msg);

    // ---- playback and A/B ----
    {
        std::vector<float> silence(256, 0.f), oL(256), oR(256);
        d.setParam(trep::kCompare, 0.f);           // the repair
        d.play(true);
        // Played past two seconds on purpose: everything before that is the
        // gap, and a working denoise empties it. Measuring there would test
        // that the repair had failed.
        for (int i = 0; i < 600; i++) d.processStereo(silence.data(), silence.data(), oL.data(), oR.data(), 256);
        float e = 0; for (float v : oL) e = std::max(e, std::fabs(v));
        snprintf(msg, sizeof msg, "peak %.3f at %.2f s", e, d.playSeconds());
        ok("play outputs the buffer, not the input", d.playing() && e > 0.01f, msg);

        d.play(false);
        d.setParam(trep::kCompare, 1.f);           // the tape
        d.play(true);
        for (int i = 0; i < 600; i++) d.processStereo(silence.data(), silence.data(), oL.data(), oR.data(), 256);
        float e2 = 0; for (float v : oL) e2 = std::max(e2, std::fabs(v));
        snprintf(msg, sizeof msg, "peak %.3f", e2);
        ok("A/B plays the tape instead", e2 > 0.01f, msg);
        d.play(false);
    }

    // ---- bypass is exact ----
    {
        d.setParam(trep::kBypass, 1.f);
        std::vector<float> in(256), oL(256), oR(256);
        for (int i = 0; i < 256; i++) in[(size_t)i] = 0.3f * (float)std::sin(i * 0.1);
        d.processStereo(in.data(), in.data(), oL.data(), oR.data(), 256);
        float worst = 0;
        for (int i = 0; i < 256; i++) worst = std::max(worst, std::fabs(in[(size_t)i] - oL[(size_t)i]));
        snprintf(msg, sizeof msg, "largest difference %.2e", worst);
        ok("bypass passes the input through sample for sample", worst == 0.f, msg);
        d.setParam(trep::kBypass, 0.f);
    }

    // ---- revert ----
    d.revert();
    ok("revert drops the repair and keeps the tape", !d.hasRepair() && d.hasTake());

    // ---- waveform ----
    {
        const auto pk = d.peaks(200, false);
        bool shaped = pk.lo.size() == 200 && pk.hi.size() == 200;
        // the gap before the music must read quieter than the music after it
        float early = 0, late = 0;
        for (int i = 10; i < 50; i++) early = std::max(early, pk.hi[(size_t)i]);
        for (int i = 120; i < 190; i++) late = std::max(late, pk.hi[(size_t)i]);
        snprintf(msg, sizeof msg, "gap %.3f, music %.3f", early, late);
        ok("the waveform shows the gap and the music", shaped && late > early * 2.f, msg);
    }

    // ---- arming again starts a new take ----
    d.arm(true);
    ok("re-arming clears the previous take", !d.hasTake());
    d.arm(false);

    // ---- a take that came from a file rather than the input ----
    {
        trep::TapeRepair f;
        f.setSampleRate(SR);

        std::vector<std::vector<float>> stereo(2, std::vector<float>((size_t)SR, 0.f));
        for (size_t i = 0; i < stereo[0].size(); i++) {
            stereo[0][i] =  0.4f * (float)std::sin(2*kPi*440.0*(double)i/SR);
            stereo[1][i] = -0.4f * (float)std::sin(2*kPi*440.0*(double)i/SR);
        }
        ok("a clip can be loaded as the take", f.setTake(stereo) && f.hasTake());
        snprintf(msg, sizeof msg, "%.2f s", f.takeSeconds());
        ok("of the right length", std::fabs(f.takeSeconds() - 1.0) < 0.01, msg);
        ok("with its channels kept apart",
           f.take().ch[0][100] * f.take().ch[1][100] < 0);

        // mono is doubled, not left half silent
        std::vector<std::vector<float>> mono(1, std::vector<float>(1000, 0.25f));
        ok("a mono file is doubled to both sides",
           f.setTake(mono) && f.take().ch.size() == 2 &&
           f.take().ch[1][10] == f.take().ch[0][10]);

        // loading again drops what was learned about the last one
        f.setTake(stereo);
        f.learn();
        f.repair();
        ok("a repair exists before reloading", f.hasProfile() && f.hasRepair());
        f.setTake(stereo);
        ok("loading a new take drops the old profile and repair",
           !f.hasProfile() && !f.hasRepair() && f.hasTake());

        std::vector<std::vector<float>> nothing;
        ok("an empty clip is refused", !f.setTake(nothing));
    }

    // ---- the repair runs somewhere else now ----
    //
    // This is the bug that killed the plug-in: the work used to happen on
    // whichever thread pressed the button, the host got no answer from its
    // window for the length of a side of tape, and it killed the process. So
    // what is checked here is not the mending - test_restore.cpp does that -
    // but that pressing REPAIR returns at once and the answer arrives later.
    {
        trep::TapeRepair f;
        f.setSampleRate(SR);
        const size_t n = (size_t)(SR * 20);        // twenty seconds
        std::vector<float> mono = transfer(n);
        f.setTake({mono, mono});
        f.learn(0.5, 1.0);

        const auto t0 = std::chrono::steady_clock::now();
        const bool started = f.startRepair();
        const double returnedInMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        char msg[120];
        snprintf(msg, sizeof msg, "REPAIR returned in %.1f ms", returnedInMs);
        ok("pressing REPAIR does not block the thread that pressed it",
           started && returnedInMs < 50.0, msg);

        ok("and says it is working while it is", f.working());

        // Nothing may be started on top of a job already running.
        ok("a second REPAIR is refused while the first runs", !f.startRepair());

        double sawProgress = 0.0;
        while (f.working()) {
            sawProgress = std::max(sawProgress, f.progress());
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        f.wait();
        snprintf(msg, sizeof msg, "reached %.0f%% before finishing", sawProgress * 100.0);
        ok("progress is reported while it runs", sawProgress > 0.0, msg);
        ok("and the repair is there when it finishes", f.hasRepair());
    }

    // ---- and it can be called off ----
    {
        trep::TapeRepair f;
        f.setSampleRate(SR);
        const size_t n = (size_t)(SR * 30);
        std::vector<float> mono = transfer(n);
        f.setTake({mono, mono});
        f.learn(0.5, 1.0);

        f.startRepair();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        const auto t0 = std::chrono::steady_clock::now();
        f.cancel();
        f.wait();
        const double stoppedInMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - t0).count();
        char msg[120];
        snprintf(msg, sizeof msg, "stopped in %.0f ms", stoppedInMs);
        ok("cancel stops the work promptly", stoppedInMs < 500.0, msg);
        ok("and leaves no half-mended repair behind", !f.hasRepair());
        ok("while the take itself is untouched", f.hasTake());
    }

    // ---- recording a side without standing over it ----
    {
        trep::TapeRepair f;
        f.setSampleRate(SR);
        std::vector<float> block(512, 0.f), oL(512), oR(512);
        for (size_t i = 0; i < block.size(); i++)
            block[i] = 0.4f * (float)std::sin(2*kPi*440.0*(double)i/SR);

        f.setFollowHost(false);
        f.setHostPlaying(true);
        ok("the host transport does nothing until FOLLOW is on", !f.armed());
        f.setHostPlaying(false);

        f.setFollowHost(true);
        f.setHostPlaying(true);
        ok("with FOLLOW on, the host starting play arms the take", f.armed());

        for (int b = 0; b < 40; b++)
            f.processStereo(block.data(), block.data(), oL.data(), oR.data(), 512);
        const double recorded = f.takeSeconds();

        f.setHostPlaying(false);
        ok("and the host stopping ends it", !f.armed());
        char msg[120];
        snprintf(msg, sizeof msg, "%.2f s in the take", recorded);
        ok("with what played kept", recorded > 0.4 && recorded < 0.5, msg);
    }

    // ---- and it finds the end of the tape by itself ----
    {
        trep::TapeRepair f;
        f.setSampleRate(SR);
        std::vector<float> block(512), quiet(512, 0.f), oL(512), oR(512);
        for (size_t i = 0; i < block.size(); i++)
            block[i] = 0.4f * (float)std::sin(2*kPi*440.0*(double)i/SR);

        f.setAutoStop(true);
        f.arm(true);
        const int musicBlocks = (int)(2.0 * SR / 512);
        for (int b = 0; b < musicBlocks; b++)
            f.processStereo(block.data(), block.data(), oL.data(), oR.data(), 512);

        int silentBlocks = 0;
        while (f.armed() && silentBlocks < (int)(30.0 * SR / 512)) {
            f.processStereo(quiet.data(), quiet.data(), oL.data(), oR.data(), 512);
            silentBlocks++;
        }
        char msg[140];
        const double stoppedAfter = silentBlocks * 512.0 / SR;
        snprintf(msg, sizeof msg, "stopped %.1f s into the run-out", stoppedAfter);
        ok("a take ends itself when the tape runs out",
           !f.armed() && stoppedAfter > 9.0 && stoppedAfter < 12.0, msg);

        snprintf(msg, sizeof msg, "%.2f s kept of %.1f s recorded",
                 f.takeSeconds(), 2.0 + stoppedAfter);
        ok("and the run-out is dropped, bar a second",
           f.takeSeconds() > 2.5 && f.takeSeconds() < 3.5, msg);
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
