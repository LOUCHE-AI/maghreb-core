// Maghreb FM - the drum synth.
//
// Two-operator FM with a pitch bend on the carrier, a band-passed noise
// layer, exponential decays and a tanh saturator - the Move module's engine,
// ported from maghreb_fm.c and already checked against its Python reference.
//
// The difference from the copy inside Maghreb Seq is that every stroke's
// twelve numbers are editable here and saved with the plug-in, because that
// is what the module is for: the sequencer plays the drums, this one builds
// them.
//
// Notes 36-46 strike the eleven strokes, the mapping the whole family uses.
//
// Framework-free, so a stroke can be rendered and measured offline.

#pragma once
#include "fm_data.h"

#include <cmath>
#include <cstdint>
#include <algorithm>
#include <atomic>

namespace mfm {

using mseq::StrokeDef;
using mseq::KitDef;
using mseq::kStrokes;
using mseq::kKits;
using mseq::kNumStrokes;
using mseq::kNumKits;
using mseq::kStrokeNames;
using mseq::kStrokeChars;

static const int kFirstNote = 36;          // D, the doum
static const int kMaxVoices = 16;

enum Param {
    kKit = 0, kTune, kDecay, kFM, kNoise, kTone, kDrive, kLevel,
    kHumanize, kSpread, kVelFM, kVelTone, kLowpass, kHighpass, kOutput,
    // which stroke the editor is pointed at, then its own twelve numbers
    kSelect,
    kSFreq, kSRatio, kSIndex, kSFmDec, kSAmpDec, kSBend, kSBendDec,
    kSNoise, kSNoiseFreq, kSNoiseQ, kSNoiseDec, kSGain, kSPan,
    kNumParams
};
static const int kFirstStrokeParam = kSFreq;
static const int kNumStrokeParams = kSPan - kSFreq + 1;

struct StrokeView { float level = 0.f; int note = -1; };
struct EngineView {
    StrokeView voices[kMaxVoices];
    float strokeLevel[kNumStrokes] = {0};   // how loud each stroke is right now
    int select = 0;
    int edited = 0;                         // how many strokes differ from factory
    int active = 0;
};

class MaghrebFM {
public:
    MaghrebFM() { reset(); }

    void setSampleRate(double sr) {
        sr_ = (sr > 0) ? sr : 44100.0;
        reset();
    }
    double sampleRate() const { return sr_; }

    // ---------------- parameters ----------------

    void setParam(int id, float v) {
        if (id < 0 || id >= kNumParams) return;
        if (p_[id] == v) return;
        p_[id] = v;
        if (id >= kFirstStrokeParam && id <= kSPan) writeSelected(id);
        if (id == kSelect) { /* the editor reads the new stroke back */ }
    }
    float getParam(int id) const {
        return (id >= 0 && id < kNumParams) ? p_[id] : 0.f;
    }

    int selected() const {
        return clampi((int)(p_[kSelect] + 0.5f), 0, kNumStrokes - 1);
    }

    // The live table. Starts as the factory one and is edited in place.
    const StrokeDef& stroke(int i) const {
        return table_[clampi(i, 0, kNumStrokes - 1)];
    }
    float strokePan(int i) const { return pan_[clampi(i, 0, kNumStrokes - 1)]; }

    // Pull the selected stroke's numbers into the edit parameters, so the
    // controls show what they are about to change rather than the previous
    // stroke's values.
    void loadSelectedInto(float* out /* kNumStrokeParams + 1 for pan */) const {
        const StrokeDef& s = table_[selected()];
        out[kSFreq - kFirstStrokeParam] = s.freq;
        out[kSRatio - kFirstStrokeParam] = s.ratio;
        out[kSIndex - kFirstStrokeParam] = s.index;
        out[kSFmDec - kFirstStrokeParam] = s.fmDecay;
        out[kSAmpDec - kFirstStrokeParam] = s.ampDecay;
        out[kSBend - kFirstStrokeParam] = s.bend;
        out[kSBendDec - kFirstStrokeParam] = s.bendDecay;
        out[kSNoise - kFirstStrokeParam] = s.noise;
        out[kSNoiseFreq - kFirstStrokeParam] = s.noiseFreq;
        out[kSNoiseQ - kFirstStrokeParam] = s.noiseQ;
        out[kSNoiseDec - kFirstStrokeParam] = s.noiseDecay;
        out[kSGain - kFirstStrokeParam] = s.gain;
        out[kSPan - kFirstStrokeParam] = pan_[selected()];
    }

    void resetStroke(int i) {
        i = clampi(i, 0, kNumStrokes - 1);
        table_[i] = kStrokes[i];
        pan_[i] = 0.f;
    }
    void resetAll() { for (int i = 0; i < kNumStrokes; i++) resetStroke(i); }

    bool strokeEdited(int i) const {
        const StrokeDef& a = table_[clampi(i, 0, kNumStrokes - 1)];
        const StrokeDef& b = kStrokes[clampi(i, 0, kNumStrokes - 1)];
        return a.freq != b.freq || a.ratio != b.ratio || a.index != b.index ||
               a.fmDecay != b.fmDecay || a.ampDecay != b.ampDecay ||
               a.bend != b.bend || a.bendDecay != b.bendDecay ||
               a.noise != b.noise || a.noiseFreq != b.noiseFreq ||
               a.noiseQ != b.noiseQ || a.noiseDecay != b.noiseDecay ||
               a.gain != b.gain || pan_[i] != 0.f;
    }

    // for state: the whole table as floats
    void saveTable(float* out) const {
        int k = 0;
        for (int i = 0; i < kNumStrokes; i++) {
            const StrokeDef& s = table_[i];
            out[k++] = s.freq; out[k++] = s.ratio; out[k++] = s.index;
            out[k++] = s.fmDecay; out[k++] = s.ampDecay; out[k++] = s.bend;
            out[k++] = s.bendDecay; out[k++] = s.noise; out[k++] = s.noiseFreq;
            out[k++] = s.noiseQ; out[k++] = s.noiseDecay; out[k++] = s.gain;
            out[k++] = pan_[i];
        }
    }
    void loadTable(const float* in) {
        int k = 0;
        for (int i = 0; i < kNumStrokes; i++) {
            StrokeDef& s = table_[i];
            s.freq = in[k++]; s.ratio = in[k++]; s.index = in[k++];
            s.fmDecay = in[k++]; s.ampDecay = in[k++]; s.bend = in[k++];
            s.bendDecay = in[k++]; s.noise = in[k++]; s.noiseFreq = in[k++];
            s.noiseQ = in[k++]; s.noiseDecay = in[k++]; s.gain = in[k++];
            pan_[i] = in[k++];
        }
    }
    static const int kTableFloats = kNumStrokes * 13;

    // ---------------- notes ----------------

    void noteOn(int note, float vel) {
        const int stroke = note - kFirstNote;
        if (stroke < 0 || stroke >= kNumStrokes) return;
        trigger(stroke, vel);
    }
    void noteOff(int) { /* one-shots: a drum is not held */ }
    void allNotesOff() { for (int v = 0; v < kMaxVoices; v++) voices_[v].active = false; }

    // Strike a stroke directly, for auditioning from the editor.
    void trigger(int stroke, float vel) {
        stroke = clampi(stroke, 0, kNumStrokes - 1);
        int slot = -1;
        for (int v = 0; v < kMaxVoices; v++)
            if (!voices_[v].active) { slot = v; break; }
        if (slot < 0) slot = steal_++ % kMaxVoices;
        startVoice(voices_[slot], stroke, vel);
    }

    int activeVoices() const {
        int n = 0;
        for (int v = 0; v < kMaxVoices; v++) if (voices_[v].active) n++;
        return n;
    }

    // ---------------- render ----------------

    // Auditioning from the editor happens on the UI thread, so it is queued
    // rather than allowed to write a voice while the audio thread is reading
    // one. Eleven strokes fit in a bitmask, and a double click inside one
    // block simply collapses to one strike.
    void queueTrigger(int stroke) {
        if (stroke < 0 || stroke >= kNumStrokes) return;
        pending_.fetch_or(1u << stroke, std::memory_order_release);
    }

    void process(float* L, float* R, int n) {
        for (int i = 0; i < n; i++) { L[i] = 0.f; R[i] = 0.f; }

        const uint32_t due = pending_.exchange(0, std::memory_order_acquire);
        if (due)
            for (int i = 0; i < kNumStrokes; i++)
                if (due & (1u << i)) trigger(i, 1.f);

        for (int i = 0; i < kNumStrokes; i++) strokeLevel_[i] = 0.f;
        for (int v = 0; v < kMaxVoices; v++) {
            if (!voices_[v].active) continue;
            renderVoice(voices_[v], L, R, n);
        }

        filterBlock(L, R, n);
        const float g = dbGain(p_[kOutput]);
        if (g != 1.f) for (int i = 0; i < n; i++) { L[i] *= g; R[i] *= g; }
        publish();
    }

    const EngineView& snapshot() const {
        return views_[viewIdx_.load(std::memory_order_acquire)];
    }

private:
    struct Voice {
        bool active = false;
        int stroke = 0, note = -1;
        double baseHz = 100, ratio = 1, index = 0, bend = 0;
        double gain = 1, vel = 1, noiseAmt = 0, pan = 0.5;
        double da = 0, dm = 0, dp = 0, dn = 0;
        double drive = 1, makeup = 1, level = 1;
        double fco = 0.1, q = 1;
        double envA = 0, envM = 0, envP = 0, envN = 0;
        double phC = 0, phM = 0, bpLo = 0, bpBand = 0;
        // Each voice carries its own noise generator, seeded at the strike.
        // Sharing one across voices makes every hit differ slightly, which
        // sounds fine but means you cannot A/B an edit - the noise moves at
        // the same time as the thing you changed. Variation is Humanize's
        // job, so it is the only thing that perturbs this seed.
        uint32_t rng = 0x1234567;
    };

    static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static float dbGain(float db) {
        if (db <= -59.9f) return 0.f;
        return (std::fabs(db) < 1e-6f) ? 1.f : std::pow(10.f, db / 20.f);
    }
    double decayCoef(double seconds) const {
        if (seconds < 0.0005) seconds = 0.0005;
        return std::exp(-6.9078 / (seconds * sr_));
    }
    double rnd() {
        rng_ ^= rng_ << 13; rng_ ^= rng_ >> 17; rng_ ^= rng_ << 5;
        return (double)(int32_t)rng_ / 2147483648.0;      // -1..1
    }

    // An edit writes straight into the live table, so a change is heard on
    // the next strike rather than needing a reload.
    void writeSelected(int id) {
        StrokeDef& s = table_[selected()];
        const float v = p_[id];
        switch (id) {
        case kSFreq: s.freq = v; break;
        case kSRatio: s.ratio = v; break;
        case kSIndex: s.index = v; break;
        case kSFmDec: s.fmDecay = v; break;
        case kSAmpDec: s.ampDecay = v; break;
        case kSBend: s.bend = v; break;
        case kSBendDec: s.bendDecay = v; break;
        case kSNoise: s.noise = v; break;
        case kSNoiseFreq: s.noiseFreq = v; break;
        case kSNoiseQ: s.noiseQ = v; break;
        case kSNoiseDec: s.noiseDecay = v; break;
        case kSGain: s.gain = v; break;
        case kSPan: pan_[selected()] = v; break;
        default: break;
        }
    }

    void startVoice(Voice& v, int stroke, float vel) {
        const StrokeDef& d = table_[stroke];
        const KitDef& k = kKits[clampi((int)(p_[kKit] + 0.5f), 0, kNumKits - 1)];

        vel = clampf(vel, 0.f, 1.f);
        // Humanize is deliberately on level and tuning rather than on timing:
        // the sequencer owns when a stroke happens, this owns how it sounds.
        const double h = p_[kHumanize];
        const double jitterTune = rnd() * h * 1.5;          // semitones
        const double jitterLvl = 1.0 + rnd() * h * 0.35;

        const double tuneMul = std::pow(2.0, (p_[kTune] + jitterTune) / 12.0);
        const double dscale = std::max(0.05f, p_[kDecay]) * k.decay;

        v.stroke = stroke;
        v.note = kFirstNote + stroke;
        v.baseHz = d.freq * k.freq * tuneMul;
        v.ratio = d.ratio;

        // velocity opens the FM index and the noise, which is what makes a
        // soft stroke read as softer rather than merely quieter
        const double velFm = 1.0 - p_[kVelFM] * (1.0 - vel);
        const double velTone = 1.0 - p_[kVelTone] * (1.0 - vel);
        v.index = d.index * p_[kFM] * k.index * velFm;
        v.bend = d.bend;
        v.gain = d.gain;
        v.vel = vel * jitterLvl;
        v.noiseAmt = d.noise * p_[kNoise] * k.noise * velTone;

        const double tilt = 0.45 + p_[kTone] * 1.35;
        const double nf = std::min((double)d.noiseFreq * tilt * tuneMul, sr_ * 0.42);

        v.da = decayCoef(d.ampDecay * dscale);
        v.dm = decayCoef(d.fmDecay * dscale);
        v.dp = decayCoef(d.bendDecay * dscale);
        v.dn = decayCoef(d.noiseDecay * dscale);

        v.drive = 1.0 + p_[kDrive] * 4.0;
        v.makeup = 1.0 / (1.0 + p_[kDrive] * 1.2);
        v.level = p_[kLevel];

        v.fco = std::min(2.0 * std::sin(3.14159265358979 * nf / sr_), 0.9);
        v.q = 1.0 / std::max(d.noiseQ, 0.5f);

        // Spread widens the kit across the stereo field from each stroke's own
        // pan, so a kit sits somewhere rather than in one spot.
        const double base = pan_[stroke];
        v.pan = 0.5 + base * 0.5 * p_[kSpread];
        v.pan = clampf((float)v.pan, 0.f, 1.f);

        v.envA = v.envM = v.envP = v.envN = 1.0;
        v.phC = v.phM = 0.0;
        v.bpLo = v.bpBand = 0.0;
        v.rng = 0x1234567u;
        if (h > 0.0) v.rng ^= (uint32_t)(hits_ * 2654435761u);
        hits_++;
        v.active = true;
    }

    void renderVoice(Voice& v, float* L, float* R, int n) {
        const double isr = 1.0 / sr_;
        const double gl = 1.0 - v.pan, gr = v.pan;
        double loudest = 0;

        for (int i = 0; i < n; i++) {
            const double hz = v.baseHz * (1.0 + v.bend * v.envP);

            v.phM += hz * v.ratio * isr;
            if (v.phM >= 1.0) v.phM -= 1.0;
            const double mod = std::sin(6.283185307179586 * v.phM) * v.index * v.envM;

            v.phC += hz * isr;
            if (v.phC >= 1.0) v.phC -= 1.0;
            double ph = v.phC + mod;
            ph -= std::floor(ph);
            const double tone = std::sin(6.283185307179586 * ph) * v.envA;

            double nz = 0.0;
            if (v.noiseAmt > 0.0001) {
                v.rng = v.rng * 1664525u + 1013904223u;
                const double w = ((double)(v.rng >> 8) - 8388608.0) / 8388608.0;
                const double hi = w - v.bpLo - v.q * v.bpBand;
                v.bpBand += v.fco * hi;
                v.bpLo += v.fco * v.bpBand;
                nz = v.bpBand * v.noiseAmt * v.envN;
            }

            double mix = (tone + nz) * v.vel * v.gain;
            v.envA *= v.da; v.envM *= v.dm; v.envP *= v.dp; v.envN *= v.dn;

            mix *= 0.42;
            mix = std::tanh(mix * v.drive) * v.makeup * v.level;

            L[i] += (float)(mix * gl);
            R[i] += (float)(mix * gr);
            loudest = std::max(loudest, std::fabs(mix));

            if (v.envA < 0.0004 && v.envN < 0.0004) { v.active = false; break; }
        }
        strokeLevel_[v.stroke] = std::max(strokeLevel_[v.stroke], (float)loudest);
    }

    // One-pole pair on the output. The module has them so a kit can be sat
    // under something else without reaching for another device.
    void filterBlock(float* L, float* R, int n) {
        const double lpHz = std::min((double)p_[kLowpass], sr_ * 0.45);
        const double hpHz = std::max(5.0, (double)p_[kHighpass]);
        const double alp = 1.0 - std::exp(-6.283185307179586 * lpHz / sr_);
        const double ahp = 1.0 - std::exp(-6.283185307179586 * hpHz / sr_);
        const bool doLp = lpHz < sr_ * 0.44;
        const bool doHp = hpHz > 6.0;
        if (!doLp && !doHp) return;

        for (int i = 0; i < n; i++) {
            double l = L[i], r = R[i];
            if (doLp) { lpL_ += alp * (l - lpL_); lpR_ += alp * (r - lpR_); l = lpL_; r = lpR_; }
            if (doHp) { hpL_ += ahp * (l - hpL_); hpR_ += ahp * (r - hpR_); l -= hpL_; r -= hpR_; }
            L[i] = (float)l; R[i] = (float)r;
        }
    }

    void publish() {
        const int cur = viewIdx_.load(std::memory_order_relaxed);
        EngineView& v = views_[1 - cur];
        int n = 0;
        for (int i = 0; i < kMaxVoices; i++) {
            v.voices[i].level = voices_[i].active ? (float)voices_[i].envA : 0.f;
            v.voices[i].note = voices_[i].active ? voices_[i].note : -1;
            if (voices_[i].active) n++;
        }
        for (int i = 0; i < kNumStrokes; i++) v.strokeLevel[i] = strokeLevel_[i];
        v.select = selected();
        v.active = n;
        int ed = 0;
        for (int i = 0; i < kNumStrokes; i++) if (strokeEdited(i)) ed++;
        v.edited = ed;
        viewIdx_.store(1 - cur, std::memory_order_release);
    }

    void reset() {
        for (int v = 0; v < kMaxVoices; v++) voices_[v] = Voice();
        lpL_ = lpR_ = hpL_ = hpR_ = 0;
        rng_ = 0x2545F491;
        steal_ = 0;
        setDefaults();
        resetAll();
    }

    void setDefaults() {
        p_[kKit] = 0.f; p_[kTune] = 0.f; p_[kDecay] = 1.f;
        p_[kFM] = 1.f; p_[kNoise] = 1.f; p_[kTone] = 0.5f;
        p_[kDrive] = 0.12f; p_[kLevel] = 0.9f;
        p_[kHumanize] = 0.3f; p_[kSpread] = 0.6f;
        p_[kVelFM] = 0.55f; p_[kVelTone] = 0.45f;
        p_[kLowpass] = 18000.f; p_[kHighpass] = 5.f;
        p_[kOutput] = 0.f;
        p_[kSelect] = 0.f;
        // the edit controls start on the first stroke's own numbers
        const StrokeDef& s = kStrokes[0];
        p_[kSFreq] = s.freq; p_[kSRatio] = s.ratio; p_[kSIndex] = s.index;
        p_[kSFmDec] = s.fmDecay; p_[kSAmpDec] = s.ampDecay; p_[kSBend] = s.bend;
        p_[kSBendDec] = s.bendDecay; p_[kSNoise] = s.noise;
        p_[kSNoiseFreq] = s.noiseFreq; p_[kSNoiseQ] = s.noiseQ;
        p_[kSNoiseDec] = s.noiseDecay; p_[kSGain] = s.gain; p_[kSPan] = 0.f;
    }

    double sr_ = 44100.0;
    float p_[kNumParams] = {0};
    StrokeDef table_[kNumStrokes];
    float pan_[kNumStrokes] = {0};

    Voice voices_[kMaxVoices];
    int steal_ = 0;
    float strokeLevel_[kNumStrokes] = {0};

    double lpL_ = 0, lpR_ = 0, hpL_ = 0, hpR_ = 0;
    uint32_t rng_ = 0x2545F491;      // humanize jitter only
    uint64_t hits_ = 0;

    std::atomic<uint32_t> pending_{0};   // strokes auditioned from the editor
    EngineView views_[2];
    std::atomic<int> viewIdx_{0};
};

} // namespace mfm
