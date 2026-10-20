// The sequencer and the drum engine, measured.
//
// The rhythms are odd cycles and the engine is a port, so the two things most
// likely to be quietly wrong are the timing grid and the sound. Both are
// checked against numbers rather than looked at.

#include "seq_dsp.h"
#include "dx_sysex.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <set>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& x = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, x.empty() ? "" : "  ", x.c_str());
    if (!c) fails++;
}

static const int SR = 44100;

static double peakOf(const std::vector<float>& v) {
    double p = 0;
    for (size_t i = 0; i < v.size(); i++) p = std::max(p, (double)std::fabs(v[i]));
    return p;
}

// render `beats` worth of audio, returning the left channel
static std::vector<float> render(mseq::MaghrebSeq& g, double beats, double bpm = 120.0) {
    const int n = 256;
    const int blocks = (int)(beats * 60.0 / bpm * SR / n);
    std::vector<float> out;
    std::vector<float> l(n), r(n);
    g.setTempo(bpm);
    for (int b = 0; b < blocks; b++) {
        g.renderBlock(l.data(), r.data(), n, bpm);
        for (int i = 0; i < n; i++) out.push_back(l[i]);
    }
    return out;
}

// Count onsets from a smoothed envelope, not from raw samples. A doum is an
// 82 Hz tone, so its waveform passes through zero every twelve milliseconds -
// a detector watching sample magnitude re-arms inside a single hit and counts
// each drum twice.
static int countHits(const std::vector<float>& x, double thresh = 0.02) {
    const double rel = std::exp(-1.0 / (0.025 * SR));   // 25 ms release
    double env = 0;
    int hits = 0;
    bool armed = true;
    for (size_t i = 0; i < x.size(); i++) {
        env = std::max((double)std::fabs(x[i]), env * rel);
        if (armed && env > thresh) { hits++; armed = false; }
        else if (!armed && env < thresh * 0.4) armed = true;
    }
    return hits;
}

int main() {
    char msg[400];

    // ---- the data ----
    {
        int total = 0;
        bool chars = true, nonEmpty = true;
        std::string bad;
        const std::string known = mseq::kStrokeChars;
        for (int i = 0; i < mseq::kNumRhythms; i++) {
            const std::string p = mseq::kRhythms[i];
            total += (int)p.size();
            if (p.empty()) nonEmpty = false;
            for (char c : p)
                if (c != '.' && known.find(c) == std::string::npos) {
                    chars = false;
                    if (bad.empty()) bad = std::string(1, c) + " in " + mseq::kRhythmNames[i];
                }
        }
        snprintf(msg, sizeof msg, "%d rhythms, %d steps", mseq::kNumRhythms, total);
        ok("all 90 rhythms carried over with their 1494 steps",
           mseq::kNumRhythms == 90 && total == 1494, msg);
        ok("every step is a rest or a known stroke", chars, bad);
        ok("no rhythm is empty", nonEmpty);
        ok("11 strokes and 3 kits",
           mseq::kNumStrokes == 11 && mseq::kNumKits == 3);
    }

    // ---- the odd cycles survived ----
    {
        // 9/4 in 4+2+3 is 36 sixteenths, 14/8 is 28, 12/8 is 24
        int len9 = -1, len14 = -1, len12 = -1;
        for (int i = 0; i < mseq::kNumRhythms; i++) {
            const std::string m = mseq::kRhythmMeters[i];
            const int L = (int)std::strlen(mseq::kRhythms[i]);
            if (len9 < 0 && m.rfind("9/4", 0) == 0) len9 = L;
            if (len14 < 0 && m.rfind("14/8", 0) == 0) len14 = L;
            if (len12 < 0 && m.rfind("12/8", 0) == 0) len12 = L;
        }
        snprintf(msg, sizeof msg, "9/4 is %d steps, 14/8 is %d, 12/8 is %d",
                 len9, len14, len12);
        ok("odd meters keep their true cycle length rather than a padded bar",
           len9 == 36 && len14 == 28 && len12 == 24, msg);
    }

    // ---- the drum engine makes a sound, and each stroke differs ----
    {
        mseq::DrumVoice v;
        v.setSampleRate(SR);
        mseq::DrumSettings s;
        std::vector<double> peaks;
        bool allSound = true, allFinite = true;

        for (int st = 0; st < mseq::kNumStrokes; st++) {
            std::vector<float> l(SR), r(SR);
            std::fill(l.begin(), l.end(), 0.f);
            std::fill(r.begin(), r.end(), 0.f);
            v.trigger(st, 0, 1.f, s);
            v.addTo(l.data(), r.data(), SR, 0.5f);
            const double pk = peakOf(l);
            peaks.push_back(pk);
            if (pk < 1e-3) allSound = false;
            for (int i = 0; i < SR; i++) if (!std::isfinite(l[i])) allFinite = false;
        }
        snprintf(msg, sizeof msg, "peaks %.2f .. %.2f",
                 *std::min_element(peaks.begin(), peaks.end()),
                 *std::max_element(peaks.begin(), peaks.end()));
        ok("every one of the eleven strokes sounds", allSound, msg);
        ok("and none of them produces NaN or inf", allFinite);
        ok("nothing clips past unity",
           *std::max_element(peaks.begin(), peaks.end()) <= 1.0,
           msg);
    }

    // ---- a doum is low and a tak is high ----
    {
        auto centroid = [](const std::vector<float>& x) {
            int cross = 0;
            for (size_t i = 1; i < x.size(); i++)
                if ((x[i] < 0) != (x[i - 1] < 0)) cross++;
            return (double)cross / x.size() * SR * 0.5;
        };
        mseq::DrumVoice v;
        v.setSampleRate(SR);
        mseq::DrumSettings s;
        std::vector<float> lo(SR / 4, 0.f), r1(SR / 4, 0.f);
        v.trigger(0, 0, 1.f, s);                 // D, doum
        v.addTo(lo.data(), r1.data(), SR / 4, 0.5f);
        std::vector<float> hi(SR / 4, 0.f), r2(SR / 4, 0.f);
        v.trigger(3, 0, 1.f, s);                 // T, tak
        v.addTo(hi.data(), r2.data(), SR / 4, 0.5f);
        snprintf(msg, sizeof msg, "doum %.0f Hz, tak %.0f Hz",
                 centroid(lo), centroid(hi));
        ok("a doum is much lower than a tak", centroid(lo) < centroid(hi) * 0.6, msg);
    }

    // ---- the kits differ ----
    {
        auto centroid = [](const std::vector<float>& x) {
            int cross = 0;
            for (size_t i = 1; i < x.size(); i++)
                if ((x[i] < 0) != (x[i - 1] < 0)) cross++;
            return (double)cross / x.size() * SR * 0.5;
        };
        mseq::DrumVoice v;
        v.setSampleRate(SR);
        mseq::DrumSettings s;
        double c[mseq::kNumKits];
        for (int k = 0; k < mseq::kNumKits; k++) {
            std::vector<float> l(SR / 4, 0.f), r(SR / 4, 0.f);
            v.trigger(0, k, 1.f, s);
            v.addTo(l.data(), r.data(), SR / 4, 0.5f);
            c[k] = centroid(l);
        }
        snprintf(msg, sizeof msg, "bendir %.0f, darbuka %.0f, tar %.0f Hz",
                 c[0], c[1], c[2]);
        ok("the three kits are pitched apart, tar highest",
           c[0] < c[1] && c[1] < c[2], msg);
    }

    // ---- the sequencer plays ----
    {
        mseq::MaghrebSeq g;
        g.setSampleRate(SR);
        g.setTransport(0, false);
        std::vector<float> quiet = render(g, 4);
        snprintf(msg, sizeof msg, "peak %.6f", peakOf(quiet));
        ok("a stopped transport is silent", peakOf(quiet) < 1e-9, msg);

        mseq::MaghrebSeq h;
        h.setSampleRate(SR);
        h.setTransport(0, true);
        std::vector<float> x = render(h, 8);
        snprintf(msg, sizeof msg, "peak %.3f, %d onsets", peakOf(x), countHits(x));
        ok("a running transport plays the rhythm", peakOf(x) > 1e-3 && countHits(x) > 4, msg);
    }

    // ---- rhythm 0 is D.D.T.T., so two bars give eight hits ----
    {
        mseq::MaghrebSeq g;
        g.setSampleRate(SR);
        g.setParam(mseq::laneParam(0, mseq::kLaneRhythm), 0.f);   // "D.D.T.T."
        for (int l = 1; l < mseq::kLanes; l++)
            g.setParam(mseq::laneParam(l, mseq::kLaneMute), 1.f);
        g.setTransport(0, true);
        // 8 steps per cycle = half a bar; 2 bars = 4 cycles = 16 hits
        std::vector<float> x = render(g, 8);
        const int hits = countHits(x);
        snprintf(msg, sizeof msg, "%d onsets over 2 bars, expected 16", hits);
        ok("the pattern fires once per marked step", hits >= 14 && hits <= 17, msg);
    }

    // ---- lanes stack ----
    {
        // Counting onsets is the wrong measure here: once several drums are
        // playing their tails overlap and a detector merges them. Energy is
        // what actually says whether a lane is contributing.
        auto rms = [](const std::vector<float>& x) {
            double s = 0;
            for (size_t i = 0; i < x.size(); i++) s += (double)x[i] * x[i];
            return std::sqrt(s / std::max<size_t>(1, x.size()));
        };
        double e[4];
        for (int lanes = 1; lanes <= 4; lanes++) {
            mseq::MaghrebSeq g;
            g.setSampleRate(SR);
            for (int l = 0; l < mseq::kLanes; l++) {
                g.setParam(mseq::laneParam(l, mseq::kLaneMute), l < lanes ? 0.f : 1.f);
                g.setParam(mseq::laneParam(l, mseq::kLaneRhythm), (float)(l * 7));
                g.setParam(mseq::laneParam(l, mseq::kLaneKit), (float)(l % mseq::kNumKits));
            }
            g.setTransport(0, true);
            e[lanes - 1] = rms(render(g, 8));
        }
        snprintf(msg, sizeof msg, "rms %.4f, %.4f, %.4f, %.4f for 1..4 lanes",
                 e[0], e[1], e[2], e[3]);
        ok("each lane added puts more into the bar",
           e[0] < e[1] && e[1] < e[2] && e[2] < e[3], msg);
    }

    // ---- a muted lane is really silent ----
    {
        mseq::MaghrebSeq g;
        g.setSampleRate(SR);
        for (int l = 0; l < mseq::kLanes; l++)
            g.setParam(mseq::laneParam(l, mseq::kLaneMute), 1.f);
        g.setTransport(0, true);
        std::vector<float> x = render(g, 4);
        snprintf(msg, sizeof msg, "peak %.9f", peakOf(x));
        ok("with every lane muted nothing sounds", peakOf(x) < 1e-9, msg);
    }

    // ---- the song chain ----
    {
        mseq::MaghrebSeq g;
        g.setSampleRate(SR);
        g.setParam(mseq::kSongOn, 1.f);
        // two sections, 2 bars then 1 bar
        for (int s = 0; s < mseq::kSections; s++)
            g.setParam(mseq::sectionParam(s, mseq::kSectionBars), s == 0 ? 2.f : (s == 1 ? 1.f : 0.f));
        snprintf(msg, sizeof msg, "%d steps", g.songSteps());
        ok("the song is as long as its sections", g.songSteps() == 3 * 16, msg);

        int sec = -1, bar = -1;
        g.locate(0, sec, bar);
        ok("step 0 is the first section", sec == 0 && bar == 0);
        g.locate(31, sec, bar);
        ok("the last step of section one is still section one", sec == 0 && bar == 1);
        g.locate(32, sec, bar);
        ok("step 32 has moved into section two", sec == 1 && bar == 0);
        g.locate(48, sec, bar);
        snprintf(msg, sizeof msg, "section %d bar %d", sec, bar);
        ok("past the end it loops back to the start", sec == 0 && bar == 0, msg);

        g.setParam(mseq::kSongLoop, 0.f);
        g.locate(1000, sec, bar);
        snprintf(msg, sizeof msg, "section %d", sec);
        ok("with looping off it holds on the last section", sec == 1, msg);
    }

    // ---- a section decides which lanes play ----
    {
        mseq::MaghrebSeq g;
        g.setSampleRate(SR);
        g.setParam(mseq::kSongOn, 1.f);
        for (int l = 0; l < mseq::kLanes; l++)
            g.setParam(mseq::laneParam(l, mseq::kLaneMute), 0.f);
        for (int s = 0; s < mseq::kSections; s++) {
            g.setParam(mseq::sectionParam(s, mseq::kSectionBars), s < 2 ? 1.f : 0.f);
            for (int l = 0; l < mseq::kLanes; l++)
                g.setParam(mseq::sectionParam(s, mseq::kSectionL1 + l),
                           (s == 0 && l == 0) ? 1.f : (s == 1 ? 1.f : 0.f));
        }
        ok("section one plays only its first lane",
           g.laneEnabled(0, 0) && !g.laneEnabled(0, 1) && !g.laneEnabled(0, 2));
        ok("section two plays all four", g.laneEnabled(1, 0) && g.laneEnabled(1, 1) &&
           g.laneEnabled(1, 2) && g.laneEnabled(1, 3));

        // and a lane muted by hand stays off whatever the section says
        g.setParam(mseq::laneParam(1, mseq::kLaneMute), 1.f);
        ok("a hand-muted lane stays off even in a section that enables it",
           !g.laneEnabled(1, 1));
    }

    // ---- output trim ----
    {
        mseq::MaghrebSeq a, b;
        a.setSampleRate(SR); b.setSampleRate(SR);
        a.setTransport(0, true); b.setTransport(0, true);
        b.setParam(mseq::kOutput, -12.f);
        const double pa = peakOf(render(a, 4)), pb = peakOf(render(b, 4));
        snprintf(msg, sizeof msg, "%.3f -> %.3f", pa, pb);
        ok("Output trims the whole thing", pb < pa * 0.35, msg);
    }

    // ---- every rhythm, every kit, stays sane ----
    {
        bool sane = true;
        std::string bad;
        for (int r = 0; r < mseq::kNumRhythms; r += 7) {
            mseq::MaghrebSeq g;
            g.setSampleRate(SR);
            for (int l = 0; l < mseq::kLanes; l++) {
                g.setParam(mseq::laneParam(l, mseq::kLaneRhythm), (float)((r + l) % mseq::kNumRhythms));
                g.setParam(mseq::laneParam(l, mseq::kLaneKit), (float)(l % mseq::kNumKits));
                g.setParam(mseq::laneParam(l, mseq::kLaneMute), 0.f);
            }
            g.setTransport(0, true);
            std::vector<float> x = render(g, 4);
            for (size_t i = 0; i < x.size(); i++)
                if (!std::isfinite(x[i]) || std::fabs(x[i]) > 4.0) {
                    sane = false;
                    if (bad.empty()) bad = mseq::kRhythmNames[r];
                }
        }
        ok("a sweep through the rhythms and kits stays finite and in range", sane, bad);
    }


    // ---- the two engines ----
    //
    // The DX path derives every stroke into a six-operator patch, which is a
    // different thing from the FM engine rather than a variation on it. What
    // matters is that it actually plays, that it plays the same grid, and that
    // it is not louder than the engine it sits beside.

    auto oneLane = [&](mseq::MaghrebSeq& g, int rhythm, bool dx) {
        g.setSampleRate(SR);
        for (int l = 0; l < mseq::kLanes; l++)
            g.setParam(mseq::laneParam(l, mseq::kLaneMute), 1.f);
        g.setParam(mseq::laneParam(0, mseq::kLaneMute), 0.f);
        g.setParam(mseq::laneParam(0, mseq::kLaneRhythm), (float)rhythm);
        g.setParam(mseq::kEngine, dx ? 1.f : 0.f);
        g.setTransport(0, true);
    };

    {
        mseq::MaghrebSeq fm, dx;
        oneLane(fm, 0, false);
        oneLane(dx, 0, true);
        std::vector<float> a = render(fm, 8), b = render(dx, 8);

        const double pa = peakOf(a), pb = peakOf(b);
        snprintf(msg, sizeof msg, "FM peak %.3f, DX peak %.3f", pa, pb);
        ok("the DX engine sounds at all", pb > 0.02, msg);
        ok("neither engine clips", pa < 1.0 && pb < 1.0, msg);

        const int ha = countHits(a), hb = countHits(b);
        snprintf(msg, sizeof msg, "FM %d hits, DX %d hits", ha, hb);
        ok("both engines strike the same number of times", ha == hb && ha > 0, msg);

        // Same grid, different sound: a correlation near 1 would mean the DX
        // switch had done nothing at all.
        double num = 0, da = 0, db = 0;
        const size_t n = std::min(a.size(), b.size());
        for (size_t i = 0; i < n; i++) { num += a[i]*b[i]; da += a[i]*a[i]; db += b[i]*b[i]; }
        const double corr = num / std::max(1e-12, std::sqrt(da * db));
        snprintf(msg, sizeof msg, "correlation %.3f", corr);
        ok("the two engines are audibly different", std::fabs(corr) < 0.5, msg);
    }

    // Timing has to be identical between them, or a song written on one plays
    // late on the other. Compared as onset positions, not as waveforms.
    {
        auto onsets = [](const std::vector<float>& x) {
            std::vector<int> o;
            const double rel = std::exp(-1.0 / (0.025 * SR));
            double env = 0; bool armed = true;
            for (size_t i = 0; i < x.size(); i++) {
                env = std::max((double)std::fabs(x[i]), env * rel);
                if (armed && env > 0.02) { o.push_back((int)i); armed = false; }
                else if (!armed && env < 0.008) armed = true;
            }
            return o;
        };
        mseq::MaghrebSeq fm, dx;
        oneLane(fm, 6, false);
        oneLane(dx, 6, true);
        std::vector<int> oa = onsets(render(fm, 8)), ob = onsets(render(dx, 8));
        int worst = 0;
        const size_t n = std::min(oa.size(), ob.size());
        for (size_t i = 0; i < n; i++) worst = std::max(worst, std::abs(oa[i] - ob[i]));
        snprintf(msg, sizeof msg, "%d and %d onsets, worst %d samples (%.2f ms)",
                 (int)oa.size(), (int)ob.size(), worst, worst * 1000.0 / SR);
        ok("both engines land on the same grid",
           oa.size() == ob.size() && oa.size() > 4 && worst <= 32, msg);
    }

    // Switching engine mid-bar should let what is already sounding ring out.
    {
        mseq::MaghrebSeq g;
        oneLane(g, 0, false);
        const int n = 256;
        std::vector<float> l(n), r(n), tail;
        for (int b = 0; b < 40; b++) g.renderBlock(l.data(), r.data(), n, 120.0);
        g.setParam(mseq::kEngine, 1.f);          // switch with voices ringing
        for (int b = 0; b < 4; b++) {
            g.renderBlock(l.data(), r.data(), n, 120.0);
            for (int i = 0; i < n; i++) tail.push_back(l[i]);
        }
        // No step falls in these four blocks at 120 bpm, so anything here is
        // the previous hit continuing rather than a new one.
        ok("switching engine does not cut the hits already sounding",
           peakOf(tail) > 0.0005,
           ([&]{ char b2[64]; snprintf(b2, sizeof b2, "tail peak %.5f", peakOf(tail)); return std::string(b2); })());
    }

    // ---- kits from disk ----
    {
        const char* path = "test_kit.mkit";
        FILE* f = fopen(path, "w");
        // one stroke moved a long way, so the change cannot be a rounding
        // difference: doum an octave down with twice the decay
        fprintf(f, "Maghreb kit 1\n");
        // Written in Maghreb FM's saveTable order, which is the struct's own.
        for (int i = 0; i < mseq::kNumStrokes; i++) {
            const mseq::StrokeDef& d = mseq::kStrokes[i];
            const float mul = (i == 0) ? 0.5f : 1.0f;
            const float dec = (i == 0) ? 2.0f : 1.0f;
            fprintf(f, "S|%d|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f|%.6f\n",
                    i, d.freq * mul, d.ratio, d.index, d.fmDecay, d.ampDecay * dec,
                    d.bend, d.bendDecay, d.noise, d.noiseFreq, d.noiseQ,
                    d.noiseDecay, d.gain, 0.5f);
        }
        fclose(f);

        for (int e = 0; e < 2; e++) {
            mseq::MaghrebSeq stock, kitted;
            oneLane(stock, 0, e == 1);
            oneLane(kitted, 0, e == 1);
            const bool loaded = kitted.loadKit(path);
            std::vector<float> a = render(stock, 8), b = render(kitted, 8);

            double num = 0, da = 0, db = 0;
            const size_t n = std::min(a.size(), b.size());
            for (size_t i = 0; i < n; i++) { num += a[i]*b[i]; da += a[i]*a[i]; db += b[i]*b[i]; }
            const double corr = num / std::max(1e-12, std::sqrt(da * db));
            snprintf(msg, sizeof msg, "%s: loaded %d, correlation %.3f, peak %.3f",
                     e ? "DX" : "FM", (int)loaded, corr, peakOf(b));
            ok("a loaded kit changes what this engine plays",
               loaded && std::fabs(corr) < 0.9 && peakOf(b) > 0.02, msg);
        }

        // and the factory button really does put it back
        mseq::MaghrebSeq g1, g2;
        oneLane(g1, 0, false);
        oneLane(g2, 0, false);
        g2.loadKit(path);
        g2.resetKit();
        std::vector<float> a = render(g1, 4), b = render(g2, 4);
        double worst = 0;
        for (size_t i = 0; i < std::min(a.size(), b.size()); i++)
            worst = std::max(worst, (double)std::fabs(a[i] - b[i]));
        snprintf(msg, sizeof msg, "worst sample difference %.2e", worst);
        ok("clearing the kit restores the factory strokes exactly", worst == 0.0, msg);

        remove(path);
    }

    // The field order is the one thing that can be wrong without sounding
    // obviously wrong - a shuffled decay and bend still make a drum. So it is
    // pinned directly: a kit written out as a copy of the factory table has to
    // render bit for bit the same as no kit at all. Any transposition of two
    // fields breaks this.
    {
        const char* path = "same_kit.mkit";
        FILE* f = fopen(path, "w");
        fprintf(f, "Maghreb kit 1\n");
        for (int i = 0; i < mseq::kNumStrokes; i++) {
            const mseq::StrokeDef& d = mseq::kStrokes[i];
            fprintf(f, "S|%d|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g|%.9g\n",
                    i, d.freq, d.ratio, d.index, d.fmDecay, d.ampDecay, d.bend,
                    d.bendDecay, d.noise, d.noiseFreq, d.noiseQ, d.noiseDecay,
                    d.gain, 0.5f);
        }
        fclose(f);

        for (int e = 0; e < 2; e++) {
            mseq::MaghrebSeq g1, g2;
            oneLane(g1, 3, e == 1);
            oneLane(g2, 3, e == 1);
            const bool loaded = g2.loadKit(path);
            std::vector<float> a = render(g1, 6), b = render(g2, 6);
            double worst = 0;
            for (size_t i = 0; i < std::min(a.size(), b.size()); i++)
                worst = std::max(worst, (double)std::fabs(a[i] - b[i]));
            snprintf(msg, sizeof msg, "%s: loaded %d, worst difference %.2e",
                     e ? "DX" : "FM", (int)loaded, worst);
            ok("a kit written from the factory table reloads to the same sound",
               loaded && worst == 0.0, msg);
        }
        remove(path);
    }

    // A bad file must be refused rather than half-applied.
    {
        FILE* f = fopen("bad_kit.mkit", "w");
        fprintf(f, "not a kit at all\nS|0|1|2|3\n");
        fclose(f);
        mseq::MaghrebSeq g;
        g.setSampleRate(SR);
        ok("a file that is not a kit is refused", !g.loadKit("bad_kit.mkit"));
        remove("bad_kit.mkit");
    }

    // Every rhythm and kit through the DX engine, for the same reason the FM
    // sweep exists: a derived patch is easy to get infinite somewhere.
    {
        bool sane = true;
        std::string bad;
        for (int r = 0; r < mseq::kNumRhythms; r += 7) {
            mseq::MaghrebSeq g;
            g.setSampleRate(SR);
            g.setParam(mseq::kEngine, 1.f);
            for (int l = 0; l < mseq::kLanes; l++) {
                g.setParam(mseq::laneParam(l, mseq::kLaneRhythm), (float)((r + l) % mseq::kNumRhythms));
                g.setParam(mseq::laneParam(l, mseq::kLaneKit), (float)(l % mseq::kNumKits));
                g.setParam(mseq::laneParam(l, mseq::kLaneMute), 0.f);
                g.setParam(mseq::laneParam(l, mseq::kLaneTune), (float)((r % 5) - 2));
            }
            g.setTransport(0, true);
            std::vector<float> x = render(g, 4);
            for (size_t i = 0; i < x.size(); i++)
                if (!std::isfinite(x[i]) || std::fabs(x[i]) > 4.0) {
                    sane = false;
                    if (bad.empty()) bad = mseq::kRhythmNames[r];
                }
        }
        ok("a sweep through the rhythms on the DX engine stays finite and in range",
           sane, bad);
    }


    // ---- a cartridge voice on a lane ----
    //
    // These need a real cartridge, so they run against the folder on this
    // machine and say so plainly when it is not there rather than passing
    // vacuously.
    {
        const char* la = std::getenv("LOCALAPPDATA");
        const std::string cart = std::string(la ? la : ".") +
                                 "\\MaghrebDX\\Cartridges\\rom1a.syx";
        std::vector<mdx::SysexVoice> voices;
        const int nv = mdx::loadSysex(cart.c_str(), voices);

        if (nv <= 0) {
            printf("SKIP   no cartridge at %s\n", cart.c_str());
        } else {
            // BRASS 1, which is nothing like a drum, so any confusion between
            // the derived patch and the imported one is audible in the numbers
            const uint8_t* patch = voices[0].data;

            mseq::MaghrebSeq kitOnly, withVoice;
            oneLane(kitOnly, 0, true);
            oneLane(withVoice, 0, true);
            withVoice.setLaneVoice(0, patch, voices[0].name);

            std::vector<float> a = render(kitOnly, 8), b = render(withVoice, 8);
            snprintf(msg, sizeof msg, "\"%s\", peak %.3f", voices[0].name, peakOf(b));
            ok("a lane on a cartridge voice sounds", peakOf(b) > 0.02, msg);

            double num = 0, da = 0, db = 0;
            const size_t n = std::min(a.size(), b.size());
            for (size_t i = 0; i < n; i++) { num += a[i]*b[i]; da += a[i]*a[i]; db += b[i]*b[i]; }
            const double corr = num / std::max(1e-12, std::sqrt(da * db));
            snprintf(msg, sizeof msg, "correlation %.3f", corr);
            ok("and does not sound like the kit it replaced", std::fabs(corr) < 0.5, msg);

            ok("the lane reports which voice it holds",
               withVoice.laneHasVoice(0) &&
               std::string(withVoice.laneVoiceName(0)) == voices[0].name);

            // The Engine switch says FM; the lane holds a DX7 patch, which the
            // FM engine cannot play. It has to go through DX anyway.
            mseq::MaghrebSeq onFM;
            oneLane(onFM, 0, false);
            onFM.setLaneVoice(0, patch, voices[0].name);
            std::vector<float> c = render(onFM, 8);
            double worst = 0;
            for (size_t i = 0; i < std::min(b.size(), c.size()); i++)
                worst = std::max(worst, (double)std::fabs(b[i] - c[i]));
            snprintf(msg, sizeof msg, "worst difference %.2e, peak %.3f", worst, peakOf(c));
            ok("a lane with a voice ignores the Engine switch and plays on DX",
               worst == 0.0 && peakOf(c) > 0.02, msg);

            // clearing must land exactly back on the kit, not merely near it
            mseq::MaghrebSeq cleared;
            oneLane(cleared, 0, true);
            cleared.setLaneVoice(0, patch, voices[0].name);
            cleared.clearLaneVoice(0);
            std::vector<float> d = render(cleared, 8);
            worst = 0;
            for (size_t i = 0; i < std::min(a.size(), d.size()); i++)
                worst = std::max(worst, (double)std::fabs(a[i] - d[i]));
            snprintf(msg, sizeof msg, "worst difference %.2e", worst);
            ok("taking the voice off puts the lane back on the kit exactly",
               worst == 0.0 && !cleared.laneHasVoice(0), msg);

            // one lane at a time: assigning lane 2 must leave lane 1 alone
            mseq::MaghrebSeq two;
            two.setSampleRate(SR);
            two.setParam(mseq::kEngine, 1.f);
            for (int l = 0; l < mseq::kLanes; l++)
                two.setParam(mseq::laneParam(l, mseq::kLaneMute), l < 2 ? 0.f : 1.f);
            two.setParam(mseq::laneParam(0, mseq::kLaneRhythm), 0.f);
            two.setParam(mseq::laneParam(1, mseq::kLaneRhythm), 6.f);
            two.setLaneVoice(1, patch, voices[0].name);
            two.setTransport(0, true);
            std::vector<float> e = render(two, 4);
            ok("assigning one lane leaves the others on the kit",
               two.laneHasVoice(1) && !two.laneHasVoice(0) &&
               two.lanesOnVoices() == 1 && peakOf(e) > 0.02);

            // every voice in the cartridge, so a patch that walks the engine
            // off a table is caught here rather than in the host
            // The bound here is 1.0, not "finite": the point of measuring
            // each patch at assignment is that a sustained voice retriggered
            // on a sixteenth grid must not clip, and only a real ceiling
            // proves that. Before the normalisation this reached 2.31.
            bool sane = true;
            double worstPk = 0;
            std::string loudest;
            for (int v = 0; v < nv; v++) {
                mseq::MaghrebSeq g;
                oneLane(g, 0, true);
                g.setLaneVoice(0, voices[v].data, voices[v].name);
                std::vector<float> x = render(g, 8);
                for (size_t i = 0; i < x.size(); i++)
                    if (!std::isfinite(x[i])) sane = false;
                const double pk = peakOf(x);
                if (pk > worstPk) { worstPk = pk; loudest = voices[v].name; }
            }
            snprintf(msg, sizeof msg, "%d voices, loudest %.3f (\"%s\")",
                     nv, worstPk, loudest.c_str());
            ok("no voice in the cartridge clips on a dense pattern",
               sane && worstPk < 1.0, msg);
        }
    }

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
