// TapeGrain - polyphonic granular engine.
//
// Ported against the Move module's own README, which documents the measured
// behaviour of the original: 6 voices sharing a 48-grain pool, three
// perceptual descriptors, a 12-agent flock, per-grain latched windows, and
// HFC onset slicing. Where a number is stated here it is that document's.
//
// Deliberately free of any plugin-framework dependency so it can be compiled
// and rendered standalone; the VST3 wrapper only forwards parameters, notes
// and buffers. That is what lets the audio be checked before it goes near a
// host.
//
// Threading: the audio thread only ever reads the sample slots. Anything
// that rebuilds them - loading, finishing a recording, re-slicing - takes
// BufferHold, which the audio thread never takes and so can never wait on.

#pragma once
#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <atomic>

namespace tg {

// Indices 0..22 are frozen: they are what earlier saved states wrote, and
// what the VST parameter table lines up with. New parameters append.
enum Param {
    kPosition = 0, kSize, kDensity, kPitch, kLevel,
    kShape, kAttack, kRelease, kSpray, kJitter, kReverse, kPanSpray, kFocus,
    kMotion, kScan, kSpan, kStepRate, kAffinity, kCohesion, kDrift, kPitchSpread,
    kFreeze, kInputMix,
    kPadMode, kSensitivity, kSpreadKeys, kVoicing, kSync, kTempo, kRate,
    kAgentCount, kOutput, kStem, kGrainCount,
    // Level per stem slot. They must stay contiguous and in slot order:
    // the mixer indexes them as kStemA + slot.
    kStemA, kStemB, kStemC, kStemD,
    kNumParams
};

enum Motion {
    mStatic = 0, mScan, mPingPong, mWalk, mStep, mSimilar, mSwarm, mPitchSwarm,
    kNumMotions
};
enum PadMode  { padPitch = 0, padSpread, padSlice, padBoth, kNumPadModes };
enum Voicing  { vMono = 0, vPoly, kNumVoicings };
enum SyncMode { syncOff = 0, syncInternal, syncHost, kNumSyncModes };
enum RateDiv  { r1_2 = 0, r1_4, r1_8, r1_8T, r1_16, r1_32, kNumRates };
enum FocusBand{ fFull = 0, fLow, fMid, fHigh, kNumFocus };

// Steps per beat for each rate division. 1/8T is three to the beat.
static const double kRatePerBeat[kNumRates] = { 0.5, 1.0, 2.0, 3.0, 4.0, 8.0 };

static const int kMaxVoices = 6;      // as measured on the module
// The module fixed this at 48. The pool is larger here and how much of it
// is used is a parameter: grain count is the difference between hearing
// separate particles and hearing a solid mass.
static const int kMaxGrains = 256;
// The module flocked a fixed 12. The pool is larger here and how many are
// actually used is a parameter, because flock size audibly changes the
// texture: a handful reads as separate voices, two dozen as a cloud.
static const int kMaxAgents = 24;

// Four sample slots. Live separates a track into Vocals, Drums, Bass and
// Others; load those here and Stem switches between them while playing.
static const int kStemSlots = 4;

// What the engine is doing right now, for the display. Published once per
// block by the audio thread into whichever of two slots the UI is not
// reading, so drawing never has to reach into live engine state.
struct GrainView {
    float pos = 0;      // 0..1 through the buffer
    float env = 0;      // this grain's own window, times its voice's envelope
};
struct EngineView {
    float agents[kMaxAgents] = {0};
    float heads[kMaxVoices] = {0};    // -1 when the voice is not sounding
    GrainView grains[kMaxGrains];
    int numAgents = 0;
    int numGrains = 0;
    int stem = 0;
    int newestVoice = -1;
    int activeSlice = -1;
    int motion = 0;
};

// The five starting points from the module. A preset is a starting point,
// not a mode: recalling one writes these values in and then gets out of the
// way. Deliberately absent - sample, position, pitch, level, input mix,
// freeze, record and tempo - so a recall never interrupts what is playing.
struct Preset {
    const char* name;
    int    sync;        // SyncMode
    int    rate;        // RateDiv
    double size, density, spray, attack, release;
};
static const Preset kPresets[] = {
    { "Tape Loop",     syncInternal, r1_4,  500,  4, 0,       5,   50 },
    { "Glitch",        syncInternal, r1_16,  45, 25, 20,      1,   40 },
    { "Pulse Bed",     syncInternal, r1_8,  250, 45, 150,    60,  400 },
    { "Cloud Pad",     syncOff,      r1_8,  400, 60, 600,   800, 3000 },
    { "Spatial Drift", syncOff,      r1_8,  900, 25, 2000, 2000, 6000 },
};
static const int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

class TapeGrain {
public:
    static const int kDescHop   = 1024;
    static const int kMaxSlices = 32;
    static const int kRootNote  = 60;     // C3 = 0 st

    // Three cheap perceptual features per frame, all from one time-domain
    // pass with no FFT.
    struct Desc {
        double loud = 0, bright = 0, rough = 0;   // normalised 0..1
        double hfc = 0;                           // raw, for onset detection
    };

    // One loaded sample and everything derived from it. Four of these, so
    // switching Stem is an index change rather than a load - which is what
    // makes it usable mid-performance.
    struct Slot {
        std::vector<float> buf;
        std::vector<Desc> desc;
        std::vector<double> slices;
    };

    TapeGrain() { reset(); }

    void setSampleRate(double sr) {
        sr_ = (sr > 0) ? sr : 44100.0;
        reset();
    }
    double sampleRate() const { return sr_; }

    // ---------------- sample ----------------

    // Which slot the display and Load are pointing at. This is a focus, not a
    // routing decision - what you hear is set by the four stem levels, so you
    // can look at one stem while all four are sounding.
    int activeSlot() const {
        return std::max(0, std::min(kStemSlots - 1, (int)(p_[kStem] + 0.5f)));
    }

    float stemLevel(int slot) const {
        return (slot >= 0 && slot < kStemSlots) ? p_[kStemA + slot] : 0.f;
    }
    bool stemAudible(int slot) const { return stemLevel(slot) > 0.0001f; }

    // Mono source. Kept by value so the host's buffer lifetime is irrelevant.
    // Called from the UI thread while the audio thread may be reading the
    // slot, so the reallocation is fenced.
    void setSample(int slot, const float* data, int frames) {
        if (slot < 0 || slot >= kStemSlots) return;
        BufferHold hold(*this);
        slots_[slot].buf.assign(data, data + std::max(0, frames));
        analyse(slot);
    }
    // convenience: the slot Stem is currently on
    void setSample(const float* data, int frames) {
        setSample(activeSlot(), data, frames);
    }

    bool hasSample() const { return !slots_[activeSlot()].buf.empty(); }
    bool slotFilled(int slot) const {
        return slot >= 0 && slot < kStemSlots && !slots_[slot].buf.empty();
    }
    int  sliceCount() const { return (int)slots_[activeSlot()].slices.size(); }
    const std::vector<double>& slices() const { return slots_[activeSlot()].slices; }
    const std::vector<float>& sampleData() const { return slots_[activeSlot()].buf; }

    // Playhead of the most recently started voice, for display. Falls back to
    // the Position knob so the marker sits somewhere sensible at rest.
    double playhead() const {
        const Voice* v = newestVoice();
        return v ? v->head : p_[kPosition];
    }
    int activeVoices() const {
        int n = 0;
        for (int i = 0; i < kMaxVoices; i++) if (voices_[i].active) n++;
        return n;
    }

    // The most recent complete snapshot. Safe from any thread: the audio
    // thread only ever writes the slot this is not returning.
    const EngineView& snapshot() const {
        return views_[viewIdx_.load(std::memory_order_acquire)];
    }

    // ---------------- parameters ----------------

    void setParam(int id, float v) {
        if (id < 0 || id >= kNumParams) return;
        if (p_[id] == v) return;
        p_[id] = v;
        if (id == kMotion) resetMotion();
        // Slicing allocates and must not run in the audio callback, so it is
        // only flagged here; refreshSlices() picks it up on the UI thread.
        if (id == kSensitivity) slicesDirty_.store(true, std::memory_order_release);
    }
    float getParam(int id) const {
        return (id >= 0 && id < kNumParams) ? p_[id] : 0.f;
    }

    // Writes a preset's values into the live parameters. Returns them through
    // `out` as well so the wrapper can tell the host what moved.
    void applyPreset(int idx, float* out /* kNumParams */) {
        if (idx < 0 || idx >= kNumPresets) return;
        const Preset& q = kPresets[idx];
        setParam(kSync, (float)q.sync);
        setParam(kRate, (float)q.rate);
        setParam(kSize, (float)q.size);
        setParam(kDensity, (float)q.density);
        setParam(kSpray, (float)q.spray);
        setParam(kAttack, (float)q.attack);
        setParam(kRelease, (float)q.release);
        if (out) for (int i = 0; i < kNumParams; i++) out[i] = p_[i];
    }

    // Host tempo, used when Sync is set to follow the host.
    void setHostTempo(double bpm) { hostTempo_ = bpm > 0 ? bpm : 0; }

    // ---------------- notes ----------------

    // Allocation: the same note first, then a free slot, then the oldest.
    void noteOn(int note, float vel) {
        if ((int)(p_[kVoicing] + 0.5f) == vMono) {
            Voice& v = voices_[0];
            startVoice(v, note, vel);
            for (int i = 1; i < kMaxVoices; i++) releaseVoice(voices_[i]);
            return;
        }
        Voice* slot = nullptr;
        for (int i = 0; i < kMaxVoices; i++)
            if (voices_[i].active && voices_[i].note == note) { slot = &voices_[i]; break; }
        if (!slot)
            for (int i = 0; i < kMaxVoices; i++)
                if (!voices_[i].active) { slot = &voices_[i]; break; }
        if (!slot) {
            slot = &voices_[0];
            for (int i = 1; i < kMaxVoices; i++)
                if (voices_[i].startStamp < slot->startStamp) slot = &voices_[i];
        }
        startVoice(*slot, note, vel);
    }

    void noteOff(int note) {
        for (int i = 0; i < kMaxVoices; i++)
            if (voices_[i].active && voices_[i].note == note && !voices_[i].releasing)
                releaseVoice(voices_[i]);
    }

    void allNotesOff() {
        for (int i = 0; i < kMaxVoices; i++) releaseVoice(voices_[i]);
    }

    // ---------------- recording ----------------

    // Arming clears the take; disarming keeps whatever was captured and
    // re-analyses it, so slicing and timbre traversal work on the new
    // material straight away.
    void setRecording(bool on, double maxSeconds = 60.0) {
        if (on == recording_) return;
        recording_ = on;
        if (on) {
            rec_.clear();
            rec_.reserve((size_t)(sr_ * maxSeconds));
            recLimit_ = (size_t)(sr_ * maxSeconds);
        } else if (!rec_.empty()) {
            const int slot = activeSlot();
            BufferHold hold(*this);
            slots_[slot].buf = rec_;
            analyse(slot);
        }
    }
    bool isRecording() const { return recording_; }
    double recordedSeconds() const { return (double)rec_.size() / sr_; }
    const std::vector<float>& take() const { return rec_; }

    // Feed the incoming block. Stereo is summed to mono, matching the engine.
    void captureInput(const float* inL, const float* inR, int n) {
        if (!recording_ || !inL) return;
        for (int i = 0; i < n && rec_.size() < recLimit_; i++) {
            float s = inR ? 0.5f * (inL[i] + inR[i]) : inL[i];
            rec_.push_back(s);
        }
        if (rec_.size() >= recLimit_) setRecording(false);   // stop at the limit
    }

    // Re-run onset detection if Sensitivity moved. UI thread only - it
    // allocates, and the module's own note is that analysis never runs in the
    // audio callback.
    void refreshSlices() {
        if (!slicesDirty_.exchange(false, std::memory_order_acq_rel)) return;
        BufferHold hold(*this);
        for (int i = 0; i < kStemSlots; i++)
            if (!slots_[i].desc.empty()) detectOnsetsIn(slots_[i], p_[kSensitivity]);
    }

    // Whether to keep the stems apart on the way out. Off by default and off
    // costs exactly what it always did: the split path is only walked when a
    // host has actually asked for the separate outputs.
    void setSplitOutputs(bool on) { split_ = on; }
    bool splitOutputs() const { return split_; }

    // Valid after process(), and only while split outputs are on. Null
    // otherwise, rather than a buffer of stale silence.
    const float* stemOutL(int slot) const {
        return (split_ && slot >= 0 && slot < kStemSlots && !stemL_[slot].empty())
               ? stemL_[slot].data() : nullptr;
    }
    const float* stemOutR(int slot) const {
        return (split_ && slot >= 0 && slot < kStemSlots && !stemR_[slot].empty())
               ? stemR_[slot].data() : nullptr;
    }

    // ---------------- render ----------------

    // Renders one block, blending the incoming audio with the grain cloud.
    // inL/inR may be null. Input Mix is the module's meaning: 0 is grains
    // only, 1 is the untouched input.
    void process(const float* inL, const float* inR, float* L, float* R, int n) {
        const float inMix = std::max(0.f, std::min(1.f, p_[kInputMix]));

        for (int i = 0; i < n; i++) {
            float dl = inL ? inL[i] : 0.f;
            float dr = inR ? inR[i] : (inL ? inL[i] : 0.f);
            L[i] = dl * inMix;
            R[i] = dr * inMix;
        }
        renderInto(L, R, n);
        applyOutputGain(L, R, n);
    }

private:
    // Adds the grain cloud on top of whatever is already in L/R.
    void renderInto(float* L, float* R, int n) {
        const float inMix = std::max(0.f, std::min(1.f, p_[kInputMix]));
        // resolved once per block; the inner loops then walk a small array
        cur_ = &slots_[activeSlot()];
        updateMix();
        if (mixN_ == 0 || inMix >= 1.f) return;

        // Someone is about to replace the buffer: skip the wet path for this
        // block rather than read memory that is being freed underneath us.
        if (hold_.load(std::memory_order_acquire)) return;
        busy_.store(true, std::memory_order_release);
        if (hold_.load(std::memory_order_acquire)) {
            busy_.store(false, std::memory_order_release);
            return;
        }

        wetL_.assign(n, 0.f);
        wetR_.assign(n, 0.f);
        if (split_)
            for (int k = 0; k < kStemSlots; k++) {
                stemL_[k].assign(n, 0.f);
                stemR_[k].assign(n, 0.f);
            }
        renderGrains(wetL_.data(), wetR_.data(), n);

        const float g = 1.f - inMix;
        for (int i = 0; i < n; i++) {
            L[i] += wetL_[i] * g;
            R[i] += wetR_[i] * g;
        }
        // The separate outputs carry the wet blend too, so the four of them
        // add up to the grain half of the main - what is left over on the
        // main is the dry input, which belongs to no single stem.
        if (split_ && g != 1.f)
            for (int k = 0; k < kStemSlots; k++)
                for (int i = 0; i < n; i++) { stemL_[k][i] *= g; stemR_[k][i] *= g; }
        busy_.store(false, std::memory_order_release);
    }

    // Master trim in dB, after the blend, so it governs everything the plug-in
    // puts out. Level only attenuates the grains; this can also make up gain,
    // which is what a quiet tape capture usually needs.
    void applyOutputGain(float* L, float* R, int n) {
        const float db = p_[kOutput];
        if (db <= -59.9f) {
            for (int i = 0; i < n; i++) { L[i] = 0.f; R[i] = 0.f; }
            return;
        }
        if (std::fabs(db) < 1e-6f) return;
        const float g = std::pow(10.f, db / 20.f);
        for (int i = 0; i < n; i++) { L[i] *= g; R[i] *= g; }
    }

public:

    // convenience for offline rendering and tests: no input, grains only
    void process(float* L, float* R, int n) {
        float saved = p_[kInputMix];
        p_[kInputMix] = 0.f;
        process(nullptr, nullptr, L, R, n);
        p_[kInputMix] = saved;
    }

    // ---------------- analysis (public: the wrapper re-runs it on load) ----

    // High-frequency-content onset detection. Attacks carry more HF energy
    // than sustains, so the first difference is squared and summed per frame,
    // differentiated to keep only rises, then peak-picked against a local
    // median with a minimum spacing. Sensitivity rescales both.
    void detectOnsets(double sensitivity) {
        for (int i = 0; i < kStemSlots; i++) detectOnsetsIn(slots_[i], sensitivity);
    }

    void detectOnsetsIn(Slot& sl, double sensitivity) {
        sl.slices.clear();
        if (sl.desc.size() < 3) {
            if (!sl.buf.empty()) sl.slices.push_back(0.0);
            return;
        }
        const double s = std::max(0.0, std::min(1.0, sensitivity));

        // rises only
        std::vector<double> d(sl.desc.size(), 0.0);
        for (size_t i = 1; i < sl.desc.size(); i++)
            d[i] = std::max(0.0, sl.desc[i].hfc - sl.desc[i - 1].hfc);

        // Between sparse onsets the local median sits at essentially zero, so
        // scaling it alone lets every ghost note through at any setting. The
        // absolute term is what actually discriminates by loudness: it is the
        // signal's own mean rise, and it fades out as sensitivity rises.
        double gMean = 0;
        for (size_t i = 0; i < d.size(); i++) gMean += d[i];
        gMean /= (double)d.size();

        const int win = 9;                       // local median window, frames
        const int gap = 1 + (int)((1.0 - s) * 8);
        std::vector<double> scratch;
        scratch.reserve(win);

        int guard = 0;
        for (size_t i = 1; i + 1 < d.size(); i++) {
            if (guard > 0) { guard--; continue; }
            size_t a = (i > (size_t)win / 2) ? i - win / 2 : 0;
            size_t z = std::min(d.size(), a + win);
            scratch.assign(d.begin() + a, d.begin() + z);
            std::sort(scratch.begin(), scratch.end());
            double med = scratch[scratch.size() / 2];

            // at sensitivity 0 only the loud hits clear the bar; at 1 nearly
            // everything above the local floor does
            double thr = med * (1.0 + 2.0 * (1.0 - s))
                       + gMean * 4.0 * (1.0 - s) + 1e-12;
            if (d[i] > thr && d[i] >= d[i - 1] && d[i] >= d[i + 1]) {
                sl.slices.push_back((double)i / (double)d.size());
                guard = gap;
                if ((int)sl.slices.size() >= kMaxSlices) break;
            }
        }
        if (sl.slices.empty()) sl.slices.push_back(0.0);
    }

private:
    // ---------------- types ----------------

    struct Grain {
        double pos = 0, inc = 1, remaining = 0, length = 1, pan = 0.5;
        double shape = 0.5;        // latched at spawn, as on the module
        int dir = 1;
        int voice = -1;
    };
    struct Agent { double p = 0.5, v = 0.0; };

    struct Voice {
        bool   active = false, releasing = false;
        int    note = -1;
        double vel = 1.0;
        double head = 0.25;        // this voice's playhead
        double basePos = 0.25;     // where its motion is centred (pad mode)
        double semis = 0.0;        // transpose from the note
        int    dir = 1;
        double stepPhase = 0;
        double spawnCountdown = 0;
        double env = 0;
        int    lastSlice = -1;     // which onset it last landed on, for display
        uint64_t startStamp = 0;
    };

    // Which slots are audible this block, and the longest of them. Rebuilt
    // once per block so the grain loop never has to test a gain or an empty
    // buffer - with one stem up it costs exactly what it did before.
    void updateMix() {
        mixN_ = 0;
        mixLen_ = 0.0;
        for (int i = 0; i < kStemSlots; i++) {
            const float g = p_[kStemA + i];
            if (g <= 0.0001f || slots_[i].buf.empty()) continue;
            mix_[mixN_].buf = &slots_[i].buf;
            mix_[mixN_].gain = g;
            mix_[mixN_].slot = i;
            mixN_++;
            mixLen_ = std::max(mixLen_, (double)slots_[i].buf.size());
        }
    }

    struct MixSrc { const std::vector<float>* buf; float gain; int slot; };

    // Exclusive claim on buf_ for the duration of a swap. The audio thread
    // never takes this, so it can never be made to wait.
    struct BufferHold {
        TapeGrain& g;
        explicit BufferHold(TapeGrain& gg) : g(gg) {
            g.hold_.store(true, std::memory_order_release);
            while (g.busy_.load(std::memory_order_acquire)) { /* brief */ }
        }
        ~BufferHold() { g.hold_.store(false, std::memory_order_release); }
    };


    const Voice* newestVoice() const {
        const Voice* best = nullptr;
        for (int i = 0; i < kMaxVoices; i++)
            if (voices_[i].active &&
                (!best || voices_[i].startStamp > best->startStamp))
                best = &voices_[i];
        return best;
    }

    // ---------------- voices ----------------

    void startVoice(Voice& v, int note, float vel) {
        v.active = true;
        v.releasing = false;
        v.note = note;
        v.vel = std::max(0.05f, vel);
        v.startStamp = ++stamp_;
        v.dir = 1;
        v.stepPhase = 0;
        v.spawnCountdown = 0;
        assignNote(v);
        v.head = v.basePos;
    }

    void releaseVoice(Voice& v) {
        if (v.active) v.releasing = true;
    }

    // Pad Mode decides what a note means: transpose, a slice, a window on the
    // buffer, or both.
    void assignNote(Voice& v) {
        const int mode = (int)(p_[kPadMode] + 0.5f);
        const int rel = v.note - kRootNote;
        v.semis = 0.0;
        v.basePos = p_[kPosition];
        v.lastSlice = -1;         // Step motion may set one later

        switch (mode) {
        case padPitch:
            v.semis = rel;
            break;
        case padSpread: {
            // the sample divided evenly across the keys, at original pitch,
            // so playing up the keyboard walks through the tape in order
            int keys = std::max(2, (int)(p_[kSpreadKeys] + 0.5f));
            int idx = ((rel % keys) + keys) % keys;
            v.basePos = (double)idx / (double)keys;
            break;
        }
        case padSlice:
        case padBoth: {
            const std::vector<double>& sl = slots_[activeSlot()].slices;
            if (!sl.empty()) {
                int n = (int)sl.size();
                int idx = ((rel % n) + n) % n;      // lowest pad -> first slice
                v.basePos = sl[idx];
                v.lastSlice = idx;
            }
            if (mode == padBoth) v.semis = rel;
            break;
        }
        }
        v.basePos = std::max(0.0, std::min(1.0, v.basePos));
    }

    // ---------------- render ----------------

    void renderGrains(float* L, float* R, int n) {
        for (int i = 0; i < n; i++) { L[i] = 0.f; R[i] = 0.f; }
        if (mixN_ == 0) return;

        const double dt = (double)n / sr_;
        // The flock is global and lives in normalised buffer space, so the
        // forces behave the same whatever the sample length.
        flock(dt);

        int live = 0;
        for (int i = 0; i < kMaxVoices; i++) if (voices_[i].active) live++;
        if (live == 0) { publishView(); return; }

        const double srms = sr_ * 0.001;
        const double grainLen = std::max((double)p_[kSize] * srms, 8.0);
        const double baseRate = std::pow(2.0, (double)p_[kPitch] / 12.0);
        const double ipiBase = sr_ / std::max((double)p_[kDensity], 0.5);
        const double aInc = 1.0 / std::max(1.0, (double)p_[kAttack] * srms);
        const double rInc = 1.0 / std::max(1.0, (double)p_[kRelease] * srms);
        const double polyNorm = 1.0 / std::sqrt((double)live);

        // grains overlap, so scale by how many can sound at once
        const double lvl = (double)p_[kLevel] / std::sqrt(std::max(
            (double)p_[kDensity] * (double)p_[kSize] * 0.001, 1.0));

        double rate[kMaxVoices];
        for (int vi = 0; vi < kMaxVoices; vi++) {
            Voice& v = voices_[vi];
            if (!v.active) continue;
            advanceMotion(v, dt);
            rate[vi] = baseRate * std::pow(2.0, v.semis / 12.0);
        }

        // One interleaved pass: envelopes and spawning have to happen at the
        // sample they belong to, or every grain in the block fires at its
        // start and the cloud turns into a pulse.
        for (int i = 0; i < n; i++) {
            for (int vi = 0; vi < kMaxVoices; vi++) {
                Voice& v = voices_[vi];
                if (!v.active) continue;

                // Attack and Release are the voice's envelope, not the
                // grain's - the grain window is Shape alone
                if (v.releasing) v.env = std::max(0.0, v.env - rInc);
                else             v.env = std::min(1.0, v.env + aInc);
                envNow_[vi] = v.env * v.vel * polyNorm;

                if (v.env > 0.0 && --v.spawnCountdown <= 0) {
                    double jit = 1.0 + (rnd() * (double)p_[kJitter] * 0.5);
                    v.spawnCountdown = ipiBase * std::max(jit, 0.1);
                    spawnGrain(vi, v, grainLen, rate[vi], srms);
                }
            }

            double l = 0, r = 0;
            if (!split_) {
                for (int g = 0; g < kMaxGrains; g++) {
                    Grain& gr = grains_[g];
                    if (gr.remaining <= 0) continue;
                    double prog = 1.0 - gr.remaining / gr.length;
                    double s = readBuffer(gr.pos) * window(prog, gr.shape)
                             * envNow_[gr.voice];
                    l += s * (1.0 - gr.pan);
                    r += s * gr.pan;
                    gr.pos += gr.inc * gr.dir;
                    gr.remaining -= 1.0;
                }
                focusFilter(l, r);
            } else {
                // Every stem hears the same grain - same window, same
                // position, same pan - so the parts stay coherent with each
                // other and with the mix. Only where the sample comes from
                // differs, which is the whole point.
                double sl[kStemSlots] = {0}, sr[kStemSlots] = {0};
                double per[kStemSlots];
                for (int g = 0; g < kMaxGrains; g++) {
                    Grain& gr = grains_[g];
                    if (gr.remaining <= 0) continue;
                    double prog = 1.0 - gr.remaining / gr.length;
                    const double w = window(prog, gr.shape) * envNow_[gr.voice];
                    readBufferSplit(gr.pos, per);
                    for (int k = 0; k < kStemSlots; k++) {
                        const double v = per[k] * w;
                        sl[k] += v * (1.0 - gr.pan);
                        sr[k] += v * gr.pan;
                    }
                    gr.pos += gr.inc * gr.dir;
                    gr.remaining -= 1.0;
                }
                for (int k = 0; k < kStemSlots; k++) {
                    focusFilterSlot(k, sl[k], sr[k]);
                    stemL_[k][i] = (float)(sl[k] * lvl);
                    stemR_[k][i] = (float)(sr[k] * lvl);
                    l += sl[k];
                    r += sr[k];
                }
            }

            L[i] = (float)(l * lvl);
            R[i] = (float)(r * lvl);
        }

        // A released voice is only freed once none of its grains are still
        // sounding, so a tail keeps its own envelope instead of being cut by
        // whatever is played next.
        for (int vi = 0; vi < kMaxVoices; vi++) {
            Voice& v = voices_[vi];
            if (!v.active || !v.releasing || v.env > 0.0) continue;
            bool ringing = false;
            for (int g = 0; g < kMaxGrains; g++)
                if (grains_[g].remaining > 0 && grains_[g].voice == vi) { ringing = true; break; }
            if (!ringing) { v.active = false; v.note = -1; }
        }

        publishView();
    }

    // Writes the display snapshot into whichever slot the UI is not reading,
    // then flips the index. One block behind at worst, which at 20-25 frames
    // a second is invisible.
    void publishView() {
        const int cur = viewIdx_.load(std::memory_order_relaxed);
        EngineView& v = views_[1 - cur];

        const int na = activeAgents();
        v.numAgents = na;
        for (int i = 0; i < na; i++) v.agents[i] = (float)agents_[i].p;

        int newest = -1;
        uint64_t best = 0;
        for (int i = 0; i < kMaxVoices; i++) {
            v.heads[i] = voices_[i].active ? (float)voices_[i].head : -1.f;
            if (voices_[i].active && voices_[i].startStamp >= best) {
                best = voices_[i].startStamp; newest = i;
            }
        }
        v.newestVoice = newest;
        v.activeSlice = (newest >= 0) ? voices_[newest].lastSlice : -1;
        v.motion = (int)(p_[kMotion] + 0.5f);
        v.stem = activeSlot();

        const double len = std::max(1.0, mixLen_);
        int n = 0;
        for (int g = 0; g < kMaxGrains; g++) {
            const Grain& gr = grains_[g];
            if (gr.remaining <= 0 || gr.voice < 0) continue;
            double prog = 1.0 - gr.remaining / gr.length;
            v.grains[n].pos = (float)(gr.pos / len);
            v.grains[n].env = (float)(window(prog, gr.shape) * envNow_[gr.voice]);
            n++;
        }
        v.numGrains = n;

        viewIdx_.store(1 - cur, std::memory_order_release);
    }

    void spawnGrain(int vi, Voice& v, double grainLen, double rate, double srms) {
        int slot = -1;
        const int limit = activeGrains();
        for (int g = 0; g < limit; g++)
            if (grains_[g].remaining <= 0) { slot = g; break; }
        if (slot < 0) return;                    // pool full: drop it

        const int mode = (int)(p_[kMotion] + 0.5f);

        // Swarm spawns from wherever the agents currently are, so the cloud
        // moves as a body rather than from one travelling point.
        double at = v.head;
        if (mode == mSwarm) {
            int k = (int)(std::fabs(rnd()) * (activeAgents() - 1));
            at = std::max(0.0, std::min(1.0,
                agents_[k].p + (v.basePos - p_[kPosition])));
        }

        // PitchSwarm flocks grain pitches around a moving centroid instead of
        // positions - Pitch Spread sets the width.
        double extraSemis = 0.0;
        if (mode == mPitchSwarm) {
            int k = (int)(std::fabs(rnd()) * (activeAgents() - 1));
            extraSemis = (agents_[k].p - 0.5) * 2.0 * (double)p_[kPitchSpread];
        }

        bool rev = (rnd() * 0.5 + 0.5) < (p_[kReverse] * 0.01);
        double start = at * mixLen_ + rnd() * p_[kSpray] * srms;
        start = std::max(0.0, std::min(mixLen_ - 1.0, start));

        Grain& g = grains_[slot];
        g.voice = vi;
        g.pos = rev ? start + grainLen : start;
        g.inc = rate * std::pow(2.0, extraSemis / 12.0);
        g.dir = rev ? -1 : 1;
        g.length = grainLen;
        g.remaining = grainLen;
        g.shape = p_[kShape];                    // latched, so a dense cloud
                                                 // morphs as new grains land
        g.pan = 0.5 + rnd() * p_[kPanSpray] * 0.5;
        g.pan = std::max(0.0, std::min(1.0, g.pan));
    }

    // Continuously variable window. At 0 a soft narrow swell, at 0.5 Hann, at
    // 1.0 instant attack and exponential decay - peaking at 1.7% of the
    // grain's length, which is the figure measured on the module.
    double window(double prog, double shape) const {
        if (prog <= 0.0 || prog >= 1.0) return 0.0;
        const double hann = 0.5 - 0.5 * std::cos(6.283185307179586 * prog);
        if (shape <= 0.5) {
            double t = shape * 2.0;                       // 0..1
            return std::pow(hann, 1.0 + (1.0 - t) * 3.0); // hann^4 .. hann
        }
        const double kPeak = 0.017;
        double perc = (prog < kPeak) ? (prog / kPeak)
                                     : std::exp(-(prog - kPeak) * 6.0);
        double t = (shape - 0.5) * 2.0;                   // 0..1
        return hann * (1.0 - t) + perc * t;
    }

    double readBuffer(double idx) const {
        // Every audible stem is read at the same index and summed. Stems from
        // one separation are time-aligned, so this is the original mix when
        // they are all up - and a live remix as soon as they are not.
        double out = 0.0;
        for (int m = 0; m < mixN_; m++) {
            const std::vector<float>& b = *mix_[m].buf;
            double i = idx;
            const double maxI = (double)b.size() - 1;
            if (i < 0) i = 0;
            if (i > maxI) i = maxI;
            const int i0 = (int)i;
            const int i1 = std::min(i0 + 1, (int)b.size() - 1);
            const double f = i - i0;
            out += (b[i0] * (1.0 - f) + b[i1] * f) * mix_[m].gain;  // linear
        }
        return out;
    }

    // The same read, kept apart per slot instead of summed. Costs the same
    // memory reads - the loop over audible stems was always here - so the only
    // difference is where the samples land.
    void readBufferSplit(double idx, double* perSlot) const {
        for (int i = 0; i < kStemSlots; i++) perSlot[i] = 0.0;
        for (int m = 0; m < mixN_; m++) {
            const std::vector<float>& b = *mix_[m].buf;
            double i = idx;
            const double maxI = (double)b.size() - 1;
            if (i < 0) i = 0;
            if (i > maxI) i = maxI;
            const int i0 = (int)i;
            const int i1 = std::min(i0 + 1, (int)b.size() - 1);
            const double f = i - i0;
            perSlot[mix_[m].slot] = (b[i0] * (1.0 - f) + b[i1] * f) * mix_[m].gain;
        }
    }

    // Three-band one-pole split at roughly 250 Hz and 2.5 kHz. Not source
    // separation, but often enough to pull a bassline or a hi-hat out.
    void focusFilter(double& l, double& r) {
        int f = (int)(p_[kFocus] + 0.5f);
        if (f == fFull) return;
        loL_ += aLo_ * (l - loL_);  loR_ += aLo_ * (r - loR_);
        hiL_ += aHi_ * (l - hiL_);  hiR_ += aHi_ * (r - hiR_);
        if (f == fLow)       { l = loL_;        r = loR_; }
        else if (f == fMid)  { l = hiL_ - loL_; r = hiR_ - loR_; }
        else                 { l = l - hiL_;    r = r - hiR_; }
    }

    // The same filter, one per slot. It is linear, so filtering each stem and
    // summing gives the same result as filtering the sum - which is what lets
    // the main output stay bit-identical whether or not the stems are split.
    void focusFilterSlot(int slot, double& l, double& r) {
        int f = (int)(p_[kFocus] + 0.5f);
        if (f == fFull) return;
        sLo_[slot][0] += aLo_ * (l - sLo_[slot][0]);
        sLo_[slot][1] += aLo_ * (r - sLo_[slot][1]);
        sHi_[slot][0] += aHi_ * (l - sHi_[slot][0]);
        sHi_[slot][1] += aHi_ * (r - sHi_[slot][1]);
        if (f == fLow)      { l = sLo_[slot][0]; r = sLo_[slot][1]; }
        else if (f == fMid) { l = sHi_[slot][0] - sLo_[slot][0];
                              r = sHi_[slot][1] - sLo_[slot][1]; }
        else                { l = l - sHi_[slot][0]; r = r - sHi_[slot][1]; }
    }

    // ---------------- motion ----------------

    void reset() {
        for (int i = 0; i < kMaxGrains; i++) grains_[i] = Grain();
        for (int i = 0; i < kMaxVoices; i++) voices_[i] = Voice();
        loL_ = loR_ = hiL_ = hiR_ = 0;
        for (int i = 0; i < kStemSlots; i++) { sLo_[i][0] = sLo_[i][1] = 0; sHi_[i][0] = sHi_[i][1] = 0; }
        rngState_ = 0x2545F491;
        stamp_ = 0;
        setDefaults();
        initAgents();
        aLo_ = 1.0 - std::exp(-6.283185307179586 * 250.0 / sr_);
        aHi_ = 1.0 - std::exp(-6.283185307179586 * 2500.0 / sr_);
        for (int i = 0; i < kMaxVoices; i++) envNow_[i] = 0.0;
    }

    void setDefaults() {
        p_[kPosition] = 0.25f; p_[kSize] = 180.f; p_[kDensity] = 30.f;
        p_[kPitch] = 0.f; p_[kLevel] = 0.8f; p_[kShape] = 0.5f;
        p_[kAttack] = 40.f; p_[kRelease] = 600.f; p_[kSpray] = 120.f;
        p_[kJitter] = 0.f; p_[kReverse] = 0.f; p_[kPanSpray] = 0.7f;
        p_[kFocus] = 0.f; p_[kMotion] = 0.f; p_[kScan] = 1.f;
        p_[kSpan] = 0.25f; p_[kStepRate] = 4.f; p_[kAffinity] = 0.3f;
        p_[kCohesion] = 0.4f; p_[kDrift] = 0.3f; p_[kPitchSpread] = 7.f;
        p_[kFreeze] = 0.f;
        p_[kInputMix] = 0.f;          // grains only, as on the module
        p_[kPadMode] = (float)padPitch;
        p_[kSensitivity] = 0.5f;
        p_[kSpreadKeys] = 16.f;
        p_[kVoicing] = (float)vPoly;
        p_[kSync] = (float)syncOff;
        p_[kTempo] = 110.f;
        p_[kRate] = (float)r1_16;
        p_[kAgentCount] = 12.f;       // the module's own flock size
        p_[kOutput] = 0.f;            // dB
        p_[kStem] = 0.f;
        p_[kGrainCount] = 48.f;       // the module's own pool size
        p_[kStemA] = 1.f;             // one stem up, so a plain sample sounds
        p_[kStemB] = 0.f;
        p_[kStemC] = 0.f;
        p_[kStemD] = 0.f;
    }

    int activeAgents() const {
        int n = (int)(p_[kAgentCount] + 0.5f);
        return std::max(2, std::min(kMaxAgents, n));
    }

    // How much of the grain pool may be in flight. Turning this down does not
    // cut grains that are already sounding - it just stops new ones being
    // handed a slot beyond the limit, so the cloud thins out rather than
    // clicking.
    int activeGrains() const {
        int n = (int)(p_[kGrainCount] + 0.5f);
        return std::max(4, std::min(kMaxGrains, n));
    }

    void resetMotion() {
        for (int i = 0; i < kMaxVoices; i++) {
            voices_[i].dir = 1;
            voices_[i].stepPhase = 0;
            voices_[i].head = voices_[i].basePos;
        }
        initAgents();
    }

    void initAgents() {
        for (int i = 0; i < kMaxAgents; i++) {
            agents_[i].p = std::max(0.0, std::min(1.0,
                (double)p_[kPosition] + rnd() * (double)p_[kSpan] * 0.5));
            agents_[i].v = rnd() * 0.01;
        }
    }

    // Steps per second, honouring Sync. Off is the free-running Step Rate.
    double stepsPerSecond() const {
        int s = (int)(p_[kSync] + 0.5f);
        if (s == syncOff) return std::max(0.01f, p_[kStepRate]);
        double bpm = (s == syncHost && hostTempo_ > 0) ? hostTempo_ : p_[kTempo];
        int r = std::max(0, std::min(kNumRates - 1, (int)(p_[kRate] + 0.5f)));
        return std::max(0.01, bpm / 60.0 * kRatePerBeat[r]);
    }

    void advanceMotion(Voice& v, double dt) {
        if (p_[kFreeze] > 0.5f) return;

        const double pos = v.basePos;
        const double span = std::max(0.005, (double)p_[kSpan]);
        const int sync = (int)(p_[kSync] + 0.5f);

        // Sync Internal/Host retriggers the playhead to Position on every
        // step, whatever the motion mode is doing between steps.
        if (sync != syncOff) {
            v.stepPhase += dt * stepsPerSecond();
            if (v.stepPhase >= 1.0) {
                v.stepPhase -= std::floor(v.stepPhase);
                v.head = pos;
                return;
            }
        }

        switch ((int)(p_[kMotion] + 0.5f)) {
        case mStatic:
            v.head = pos;
            break;
        case mScan: {
            // decoupled from pitch: the head can crawl, freeze or run
            // backwards while grains stay at whatever pitch was asked for
            v.head += (double)p_[kScan] * dt * 0.25;
            double lo = pos - span * 0.5, hi = pos + span * 0.5;
            double w = hi - lo;
            if (w > 1e-9) {
                while (v.head > hi) v.head -= w;
                while (v.head < lo) v.head += w;
            }
            break;
        }
        case mPingPong: {
            v.head += v.dir * std::fabs((double)p_[kScan]) * dt * 0.25;
            double lo = pos - span * 0.5, hi = pos + span * 0.5;
            if (v.head > hi) { v.head = hi; v.dir = -1; }
            if (v.head < lo) { v.head = lo; v.dir = 1; }
            break;
        }
        case mWalk:
            // scaled to Span, because a raw brownian walk only spreads with
            // the square root of time and barely moves over musical timescales.
            // Bounded by half a Span either side, so Span means the same total
            // width here as it does in Scan and PingPong - and so the band
            // drawn on the waveform is the truth for every mode.
            v.head += rnd() * (double)p_[kDrift] * span * dt * 8.0;
            v.head = std::max(pos - span * 0.5,
                              std::min(pos + span * 0.5, v.head));
            break;
        case mStep:
            v.stepPhase += dt * stepsPerSecond();
            if (v.stepPhase >= 1.0) {
                v.stepPhase -= std::floor(v.stepPhase);
                const std::vector<double>& sl = slots_[activeSlot()].slices;
                if (!sl.empty()) {
                    int idx = (int)(std::fabs(rnd()) * (sl.size() - 1));
                    v.head = sl[idx];
                    v.lastSlice = idx;
                } else {
                    v.head = pos + rnd() * span;
                }
            }
            break;
        case mSimilar:
            v.stepPhase += dt * stepsPerSecond();
            if (v.stepPhase >= 1.0) {
                v.stepPhase -= std::floor(v.stepPhase);
                v.head = nearestByTimbre(v.head);
            }
            break;
        case mSwarm:
            v.head = flockCentre() + (pos - p_[kPosition]);
            break;
        case mPitchSwarm:
            v.head = pos;            // position holds; the flock carries pitch
            break;
        }
        v.head = std::max(0.0, std::min(1.0, v.head));
    }

    double flockCentre() const {
        double m = 0;
        const int na = activeAgents();
        for (int i = 0; i < na; i++) m += agents_[i].p;
        return m / na;
    }

    // Boids reduced to one dimension: cohesion toward the flock centre,
    // separation from crowded neighbours, alignment with mean velocity.
    void flock(double dt) {
        if (p_[kFreeze] > 0.5f) return;
        double mean = 0, meanV = 0;
        const int na = activeAgents();
        for (int i = 0; i < na; i++) { mean += agents_[i].p; meanV += agents_[i].v; }
        mean /= na; meanV /= na;

        // Drift moves the flock centre; it scales linearly with the setting.
        driftPhase_ += rnd() * (double)p_[kDrift] * dt * 0.5;
        driftPhase_ = std::max(-1.0, std::min(1.0, driftPhase_));
        const double target = std::max(0.0, std::min(1.0,
            (double)p_[kPosition] + driftPhase_));

        for (int i = 0; i < na; i++) {
            Agent& a = agents_[i];
            double cohere = (mean - a.p) * (double)p_[kCohesion];
            double align  = (meanV - a.v) * 0.5;
            double toward = (target - a.p) * (double)p_[kAffinity];
            double sep = 0;
            for (int j = 0; j < na; j++) {
                if (i == j) continue;
                double d = a.p - agents_[j].p;
                if (std::fabs(d) < 0.01)
                    sep += (d >= 0 ? 1 : -1) * 0.01 * (1.0 - (double)p_[kCohesion]);
            }
            a.v += (cohere + toward + sep + align) * dt;
            a.v += rnd() * (double)p_[kDrift] * dt * 0.5;
            a.v *= 0.98;
            a.p = std::max(0.0, std::min(1.0, a.p + a.v * dt * 10.0));
        }
    }

    // Jump to the frame that sounds most like the current one - the
    // concatenative idea. Affinity sets k: at 0 it always takes the single
    // nearest match, higher values pick at random among the k closest so
    // repeats never lock into a loop.
    double nearestByTimbre(double from) {
        const std::vector<Desc>& desc_ = slots_[activeSlot()].desc;
        const size_t N = desc_.size();
        if (N < 2) return from;
        size_t cur = (size_t)(std::max(0.0, std::min(0.999, from)) * N);
        if (cur >= N) cur = N - 1;

        const int k = 1 + (int)((double)p_[kAffinity] * 15.0 + 0.5);
        // brightness is weighted highest: it is what the ear tracks most
        const double wl = 1.0, wb = 2.0, wr = 1.0;

        std::pair<double, size_t> best[16];
        int filled = 0;
        for (size_t i = 0; i < N; i++) {
            if (i == cur) continue;
            double dl = (desc_[i].loud   - desc_[cur].loud)   * wl;
            double db = (desc_[i].bright - desc_[cur].bright) * wb;
            double dr = (desc_[i].rough  - desc_[cur].rough)  * wr;
            double d = dl * dl + db * db + dr * dr;
            if (filled < k) {
                best[filled++] = std::make_pair(d, i);
                std::sort(best, best + filled);
            } else if (d < best[k - 1].first) {
                best[k - 1] = std::make_pair(d, i);
                std::sort(best, best + k);
            }
        }
        if (filled == 0) return from;
        int pick = (int)(std::fabs(rnd()) * (filled - 1) + 0.5);
        pick = std::max(0, std::min(filled - 1, pick));
        return (double)best[pick].second / (double)N;
    }

    // Three cheap perceptual features, all from one time-domain pass with no
    // FFT, each normalised to 0-1 so none dominates the distance.
    void analyse(int slot) {
        Slot& sl = slots_[slot];
        sl.desc.clear();
        sl.slices.clear();
        if ((int)sl.buf.size() < kDescHop * 3) {
            if (!sl.buf.empty()) sl.slices.push_back(0.0);
            return;
        }

        const int hops = (int)sl.buf.size() / kDescHop;
        sl.desc.reserve(hops);
        for (int h = 0; h < hops; h++) {
            const float* p = &sl.buf[(size_t)h * kDescHop];
            double energy = 0, diffEnergy = 0;
            int cross = 0;
            float prev = p[0];
            for (int i = 1; i < kDescHop; i++) {
                float s = p[i];
                double d = (double)s - prev;
                energy += (double)s * s;
                diffEnergy += d * d;
                if ((s < 0) != (prev < 0)) cross++;
                prev = s;
            }
            Desc dsc;
            double rms = std::sqrt(energy / kDescHop);
            dsc.loud = std::log10(rms + 1e-6);                  // log RMS
            dsc.bright = diffEnergy / (energy + 1e-12);         // HF ratio
            dsc.rough = (double)cross / kDescHop;               // ZCR
            dsc.hfc = diffEnergy;                               // raw, for onsets
            sl.desc.push_back(dsc);
        }

        normalise(sl, &Desc::loud);
        normalise(sl, &Desc::bright);
        normalise(sl, &Desc::rough);

        // Pre-sized so detectOnsets never has to allocate when Sensitivity is
        // turned; it can then be re-run without touching the heap.
        sl.slices.reserve(kMaxSlices);
        detectOnsetsIn(sl, p_[kSensitivity]);
    }

    void normalise(Slot& sl, double Desc::*field) {
        double lo = 1e30, hi = -1e30;
        for (size_t i = 0; i < sl.desc.size(); i++) {
            double v = sl.desc[i].*field;
            lo = std::min(lo, v); hi = std::max(hi, v);
        }
        double range = hi - lo;
        if (range < 1e-12) {
            for (size_t i = 0; i < sl.desc.size(); i++) sl.desc[i].*field = 0.5;
            return;
        }
        for (size_t i = 0; i < sl.desc.size(); i++)
            sl.desc[i].*field = (sl.desc[i].*field - lo) / range;
    }

    // xorshift: deterministic, so a render can be compared run to run
    double rnd() {
        rngState_ ^= rngState_ << 13;
        rngState_ ^= rngState_ >> 17;
        rngState_ ^= rngState_ << 5;
        return ((double)(int32_t)rngState_ / 2147483648.0);   // -1..1
    }

    // ---------------- state ----------------
    double sr_ = 44100.0;
    double hostTempo_ = 0;
    float p_[kNumParams] = {0};

    Slot slots_[kStemSlots];
    Slot* cur_ = &slots_[0];        // the slot the display is focused on
    MixSrc mix_[kStemSlots];        // the slots that are audible this block
    int mixN_ = 0;
    double mixLen_ = 0.0;

    Grain grains_[kMaxGrains];
    Voice voices_[kMaxVoices];
    Agent agents_[kMaxAgents];
    uint64_t stamp_ = 0;
    double driftPhase_ = 0;

    double loL_ = 0, loR_ = 0, hiL_ = 0, hiR_ = 0, aLo_ = 0.03, aHi_ = 0.3;
    double sLo_[kStemSlots][2] = {{0}}, sHi_[kStemSlots][2] = {{0}};
    // Filled only when the host has asked for the separate outputs.
    std::vector<float> stemL_[kStemSlots], stemR_[kStemSlots];
    bool split_ = false;
    uint32_t rngState_ = 0x2545F491;

    double envNow_[kMaxVoices] = {0};      // this sample's gain, per voice
    std::vector<float> wetL_, wetR_;
    std::vector<float> rec_;
    size_t recLimit_ = 0;
    bool recording_ = false;

    EngineView views_[2];
    std::atomic<int> viewIdx_{0};          // which slot the UI may read

    std::atomic<bool> busy_{false};        // audio thread is inside the wet path
    std::atomic<bool> hold_{false};        // a slot swap is pending or running
    std::atomic<bool> slicesDirty_{false}; // Sensitivity moved; re-slice off-thread
};

} // namespace tg
