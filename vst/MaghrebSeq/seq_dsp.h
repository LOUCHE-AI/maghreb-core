// Maghreb Seq - four rhythm lanes through the Maghreb FM drum engine, with a
// song chain on top.
//
// Each lane owns a rhythm, a kit and its own voice settings, so a groove is
// built by stacking bendir against darbuka against tar rather than by one
// pattern of mixed strokes. The song chain then says which lanes play, and
// for how long, so it can run past a single cycle into an arrangement.
//
// Position is read from the host's transport rather than counted, for the
// same reason the Move module reads it: the rhythms are odd cycles - 9/4,
// 14/8, 12/8 - and a counted step drifts against the Set the moment you loop
// or scrub. Deriving the step from song position keeps every lane and every
// instance phase-locked, which is what makes stacking several of these work.
//
// Framework-free, so a whole song can be rendered and measured offline.

#pragma once
#include "seq_fm.h"
#include "seq_data.h"
#include "dx_engine.h"
#include "dx_patch.h"
#include "kit_preset.h"
#include "soft_clip.h"

#include <vector>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <atomic>
#include <string>
#include <cstring>

namespace mseq {

static const int kLanes    = 4;
static const int kSections = 8;
static const int kMaxVoices = 24;      // shared across every lane

// Parameters. Lane and section blocks are contiguous and indexed by
// kLane0 + lane * kLaneStride + offset, so the wrapper can build its table in
// a loop rather than by hand.
enum Param {
    kSwing = 0, kMaster, kOutput, kSongOn, kSongLoop,
    kNumGlobal,

    kLane0 = kNumGlobal,
    kLaneRhythm = 0, kLaneKit, kLaneLevel, kLanePan, kLaneTune,
    kLaneDecay, kLaneDrive, kLaneMute,
    kLaneStride,

    kSection0 = kLane0 + kLanes * kLaneStride,
    kSectionBars = 0, kSectionL1, kSectionL2, kSectionL3, kSectionL4,
    kSectionStride,

    // Appended after the section block so every id above keeps its number and
    // a Set saved before the DX engine existed still loads.
    kEngine = kSection0 + kSections * kSectionStride,
    kTone, kVelFM,
    kNumParams
};

static inline int laneParam(int lane, int off) { return kLane0 + lane * kLaneStride + off; }
static inline int sectionParam(int s, int off) { return kSection0 + s * kSectionStride + off; }

// What the engine is doing, for the display.
struct LaneView {
    int step = 0;          // where in its own cycle
    int length = 16;
    bool hit = false;      // fired on this block
    bool playing = false;  // enabled by the current section
    char stroke = '.';
};
struct EngineView {
    LaneView lanes[kLanes];
    int section = 0;
    int barInSection = 0;
    int sectionBars = 1;
    bool running = false;
    int engine = 0;        // 0 = FM, 1 = DX
    int voices = 0;
};

class MaghrebSeq {
public:
    MaghrebSeq() { resetKit(); reset(); }

    void setSampleRate(double sr) {
        sr_ = (sr > 0) ? sr : 44100.0;
        for (int i = 0; i < kMaxVoices; i++) voices_[i].setSampleRate(sr_);
        dx_.setSampleRate(sr_);
        reset();
    }

    bool dxEngine() const { return p_[kEngine] >= 0.5f; }
    double sampleRate() const { return sr_; }

    void setParam(int id, float v) {
        if (id < 0 || id >= kNumParams) return;
        p_[id] = v;
    }
    float getParam(int id) const {
        return (id >= 0 && id < kNumParams) ? p_[id] : 0.f;
    }

    int rhythmOf(int lane) const {
        return clampi((int)(p_[laneParam(lane, kLaneRhythm)] + 0.5f), 0, kNumRhythms - 1);
    }
    int cycleLength(int lane) const {
        return (int)std::strlen(kRhythms[rhythmOf(lane)]);
    }

    // How many 16ths the whole song lasts. Sections with zero bars are
    // skipped, so a two-section song is just two sections long.
    int songSteps() const {
        int total = 0;
        for (int s = 0; s < kSections; s++) total += sectionBars(s) * 16;
        return total;
    }
    int sectionBars(int s) const {
        return clampi((int)(p_[sectionParam(s, kSectionBars)] + 0.5f), 0, 16);
    }
    bool laneEnabled(int section, int lane) const {
        if (!songOn()) return p_[laneParam(lane, kLaneMute)] < 0.5f;
        if (p_[laneParam(lane, kLaneMute)] >= 0.5f) return false;
        return p_[sectionParam(section, kSectionL1 + lane)] >= 0.5f;
    }
    bool songOn() const { return p_[kSongOn] >= 0.5f; }

    // Which section a given 16th falls in, and how far into it.
    void locate(int64_t step, int& section, int& barInSection) const {
        section = 0; barInSection = 0;
        const int total = songSteps();
        if (total <= 0) return;

        int64_t pos = step;
        if (p_[kSongLoop] >= 0.5f) {
            pos %= total;
            if (pos < 0) pos += total;
        } else if (pos >= total) {
            pos = total - 1;                 // hold on the last section
        }
        for (int s = 0; s < kSections; s++) {
            const int len = sectionBars(s) * 16;
            if (len <= 0) continue;
            if (pos < len) { section = s; barInSection = (int)(pos / 16); return; }
            pos -= len;
        }
        section = kSections - 1;
    }

    // ---- what the last block struck ----
    //
    // Published so the wrapper can send the same hits out as MIDI. Read from
    // the pattern the engine just played rather than worked out a second time:
    // notes that leave and drums that sound are then the same decision, and a
    // mute or a song section cannot apply to one and not the other.
    struct Fired {
        int offset = 0;      // samples into the block, swing included
        int lane = 0;
        int stroke = 0;
    };
    static const int kMaxFiredPerBlock = 32;

    int firedCount() const { return firedN_; }
    const Fired& fired(int i) const {
        static const Fired none;
        return (i >= 0 && i < firedN_) ? fired_[i] : none;
    }

    // ---- the pattern, as something other than sound ----
    //
    // A rhythm is a description before it is a noise, and there are three
    // things worth doing with the description: play it, write it down as MIDI,
    // or render it. All three read the pattern through here, so what gets
    // exported cannot drift from what is heard - which it would, the first
    // time a mute or a swing setting was handled in one place and not the
    // other.

    struct Hit {
        int64_t step = 0;        // 16ths from the start
        int lane = 0;
        int stroke = 0;          // index into kStrokeNames
        double lateSteps = 0.0;  // what swing does to it, in steps
    };

    // Whether `lane` sounds on `step`, and on which stroke: the decision
    // fireStep() makes, without the sound or the bookkeeping around it.
    bool hitAt(int64_t step, int lane, int& stroke, int& idx) const {
        stroke = -1;
        idx = 0;
        if (lane < 0 || lane >= kLanes) return false;
        const char* pat = kRhythms[rhythmOf(lane)];
        const int len = (int)std::strlen(pat);
        if (len <= 0) return false;

        idx = (int)(step % len);
        if (idx < 0) idx += len;

        int section = 0, bar = 0;
        locate(step, section, bar);
        if (!laneEnabled(section, lane)) return false;

        const char c = pat[idx];
        if (c == '.') return false;
        stroke = strokeIndex(c);
        return stroke >= 0;
    }

    // One full turn of the pattern, in 16ths.
    //
    // With the song on that is the song. Without it, it is where the lanes
    // come back into phase - and because these are odd cycles, four lanes of
    // 9, 12, 14 and 19 sixteenths agree only after 4788 of them, which is five
    // minutes of export nobody asked for. Past the cap it falls back to the
    // longest single lane, rounded up to a bar: a loop that is honest about
    // being one lane's cycle rather than a five-minute file.
    static const int64_t kMaxCycleSteps = 64 * 16;

    int64_t cycleSteps() const {
        if (songOn()) {
            const int64_t s = songSteps();
            return s > 0 ? s : 16;
        }
        int64_t together = 1, longest = 0;
        bool any = false;
        for (int lane = 0; lane < kLanes; lane++) {
            if (!laneEnabled(0, lane)) continue;
            const int64_t len = cycleLength(lane);
            if (len <= 0) continue;
            any = true;
            longest = std::max(longest, len);
            together = lcm64(together, len);
        }
        if (!any) return 16;
        if (together <= kMaxCycleSteps) return together;
        return ((longest + 15) / 16) * 16;
    }

    // Every hit in the next `steps` sixteenths, in time order.
    std::vector<Hit> pattern(int64_t steps) const {
        std::vector<Hit> out;
        if (steps <= 0) return out;
        const double swing = clampf(p_[kSwing], 0.f, 1.f);

        for (int64_t step = 0; step < steps; step++) {
            for (int lane = 0; lane < kLanes; lane++) {
                int stroke = -1, idx = 0;
                if (!hitAt(step, lane, stroke, idx)) continue;
                Hit h;
                h.step = step;
                h.lane = lane;
                h.stroke = stroke;
                // the same two thirds of a step process() holds it back by
                h.lateSteps = (swing > 0 && (idx % 2) == 1) ? swing * 0.66 : 0.0;
                out.push_back(h);
            }
        }
        return out;
    }

    // ---- transport ----

    // Called once per block with the host's position in quarter notes and
    // whether it is running. Everything else follows from that.
    void setTransport(double beats, bool running) {
        beats_ = beats;
        running_ = running;
        if (!running) { lastStep_ = -1; pendingN_ = 0; }
    }

    // What the host calls. Silent while an offline render has the engine: it
    // cannot be in two places on the timeline at once, and silence is the
    // honest thing to say about that rather than a stream of hits from
    // whatever bar the render is passing through.
    void process(float* L, float* R, int n) {
        if (busy_.load(std::memory_order_acquire)) {
            for (int i = 0; i < n; i++) { L[i] = 0.f; R[i] = 0.f; }
            return;
        }
        renderInto(L, R, n);
    }

    // The engine itself, with no opinion about who is asking. The offline
    // renders go through here rather than process(), which would otherwise
    // hand them the silence the flag is there to produce.
    void renderInto(float* L, float* R, int n) {
        for (int i = 0; i < n; i++) { L[i] = 0.f; R[i] = 0.f; }
        firedN_ = 0;

        if (running_) {
            // Step boundaries inside this block, so a hit lands on its own
            // sample rather than at the top of the buffer.
            const double stepsPerBeat = 4.0;              // 16ths
            const double beatsPerBlock = (double)n / (sr_ * secPerBeat());
            const double startStep = beats_ * stepsPerBeat;
            const double endStep = (beats_ + beatsPerBlock) * stepsPerBeat;

            int64_t first = (int64_t)std::floor(startStep);
            if (startStep == std::floor(startStep)) first -= 1;   // include it below

            for (int64_t s = first + 1; s <= (int64_t)std::floor(endStep); s++) {
                if (s < 0) continue;
                if (s == lastStep_) continue;
                lastStep_ = s;
                const double frac = (s / stepsPerBeat - beats_) / std::max(1e-9, beatsPerBlock);
                int off = (int)(frac * n + 0.5);
                off = clampi(off, 0, n - 1);
                fireStep(s, off);
            }
        }

        // The block is rendered in pieces, one per hit due inside it, so a drum
        // starts on the sample it was written for.
        //
        // It used to be struck at the top of whichever block noticed it. On a
        // 256 sample buffer that is five milliseconds of quantising - audible
        // as a stiffness on fast rhythms - and it threw the swing offset away
        // entirely, which is why the Swing control could be turned up without
        // anything happening.
        sortPending();
        int pos = 0;
        for (int i = 0; i < pendingN_; i++) {
            const int64_t at = pending_[i].at;
            if (at >= n) break;                    // still owed to a later block

            // Cut on the DX engine's own grid rather than the exact sample.
            //
            // That engine renders in chunks of 64 and keeps the remainder, so a
            // note handed to it part way through one does not start until the
            // next - it cannot be placed more precisely than that. Striking the
            // FM voices exactly while the DX voices land up to 63 samples later
            // would pull the two engines apart, and playing the same rhythm on
            // either has to give the same grid. So both go on the 64 sample
            // grid: still four times tighter than the block this used to
            // quantise to, and swing is thousands of samples, not tens.
            const int64_t grid = (at / kTriggerGrid) * kTriggerGrid;
            const int cut = (int)std::max<int64_t>(pos, grid);
            renderVoices(L + pos, R + pos, cut - pos);
            pos = cut;

            trigger(pending_[i].lane, pending_[i].stroke);
            if (firedN_ < kMaxFiredPerBlock) {
                Fired& f = fired_[firedN_++];
                f.offset = cut;
                f.lane = pending_[i].lane;
                f.stroke = pending_[i].stroke;
            }
        }
        renderVoices(L + pos, R + pos, n - pos);
        advancePending(n);

        const float master = p_[kMaster];
        const float g = dbGain(p_[kOutput]) * master;
        if (g != 1.f) for (int i = 0; i < n; i++) { L[i] *= g; R[i] *= g; }
        // Four lanes, and any of them may be holding a cartridge voice.
        mdsp::softClipBlock(L, R, n);

        publish();
    }

    // ---- rendering it, rather than playing it ----
    //
    // On this engine rather than a copy of it, because a copy is not this: an
    // edited stroke, a cartridge voice on a lane, a kit loaded from disk - all
    // of that would have to be carried across, and whatever was missed would
    // come out as a file that does not sound like the plug-in.
    bool busy() const { return busy_.load(std::memory_order_acquire); }

    // The pattern once through, plus the tail it rings out into.
    bool renderPattern(std::vector<float>& L, std::vector<float>& R,
                       int64_t steps, double bpm, double tailSeconds = 1.5) {
        if (steps <= 0 || busy()) return false;
        Busy guard(busy_);

        const double savedBeats = beats_, savedSpb = secPerBeat_;
        const bool savedRunning = running_;
        const int64_t savedStep = lastStep_;

        silenceAll();
        setTempo(bpm);
        beats_ = 0.0;
        running_ = true;
        lastStep_ = -1;

        const double secPerStep = secPerBeat_ * 0.25;
        const int64_t body = (int64_t)std::llround((double)steps * secPerStep * sr_);
        const int64_t tail = (int64_t)std::max(0.0, tailSeconds * sr_);
        if (body <= 0) { restore(savedBeats, savedSpb, savedRunning, savedStep); return false; }

        L.assign((size_t)(body + tail), 0.f);
        R.assign((size_t)(body + tail), 0.f);

        const int64_t block = 256;
        for (int64_t at = 0; at < body + tail; at += block) {
            const int n = (int)std::min(block, body + tail - at);
            // Past the end of the pattern the transport stops, so the last
            // hits ring out instead of the next bar starting underneath them.
            if (at >= body) running_ = false;
            renderBlock(&L[(size_t)at], &R[(size_t)at], n, bpm);
        }

        silenceAll();
        restore(savedBeats, savedSpb, savedRunning, savedStep);
        return true;
    }

    // One stroke on its own, struck as the lane would strike it.
    bool renderStroke(std::vector<float>& L, std::vector<float>& R,
                      int lane, int stroke, double seconds) {
        if (lane < 0 || lane >= kLanes || stroke < 0 || stroke >= kNumStrokes) return false;
        if (seconds <= 0 || busy()) return false;
        Busy guard(busy_);

        const double savedBeats = beats_, savedSpb = secPerBeat_;
        const bool savedRunning = running_;
        const int64_t savedStep = lastStep_;

        silenceAll();
        running_ = false;                 // no pattern underneath it
        const int64_t total = (int64_t)(seconds * sr_);
        L.assign((size_t)total, 0.f);
        R.assign((size_t)total, 0.f);

        trigger(lane, stroke);

        const int64_t block = 256;
        for (int64_t at = 0; at < total; at += block) {
            const int n = (int)std::min(block, total - at);
            renderInto(&L[(size_t)at], &R[(size_t)at], n);
        }

        silenceAll();
        restore(savedBeats, savedSpb, savedRunning, savedStep);
        return true;
    }

    // For offline rendering: advance the transport ourselves.
    void renderBlock(float* L, float* R, int n, double bpm) {
        secPerBeat_ = 60.0 / std::max(20.0, bpm);
        renderInto(L, R, n);
        beats_ += (double)n / (sr_ * secPerBeat_);
    }

    const EngineView& snapshot() const {
        return views_[viewIdx_.load(std::memory_order_acquire)];
    }
    void setTempo(double bpm) { secPerBeat_ = 60.0 / std::max(20.0, bpm); }
    // What the host last said, for anything written down in musical time.
    double tempo() const { return 60.0 / std::max(1e-9, secPerBeat_); }

    int activeVoices() const {
        int n = dx_.activeVoices();
        for (int v = 0; v < kMaxVoices; v++) if (voices_[v].active()) n++;
        return n;
    }

    // ---- kits on disk ----
    //
    // The same .mkit that Maghreb FM and Maghreb DX write, so a kit edited
    // stroke by stroke over there is the kit this sequencer plays. Only the
    // stroke rows are taken: the parameter block belongs to those plug-ins'
    // own controls, and this one has lanes instead.

    bool loadKit(const char* path) {
        StrokeDef tmp[kNumStrokes];
        float pan[kNumStrokes];
        for (int i = 0; i < kNumStrokes; i++) { tmp[i] = kStrokes[i]; pan[i] = 0.5f; }
        bool any = false;

        const bool ok = kit::loadKit(path,
            [](const std::string&, double) {},
            [&](int idx, const float* v) {
                if (idx < 0 || idx >= kNumStrokes || !v) return;
                // Field order is Maghreb FM's saveTable, which is the struct's
                // own order. Written out here rather than memcpy'd, because
                // the two plug-ins must agree on it and a silent mismatch
                // would only sound slightly wrong.
                StrokeDef& d = tmp[idx];
                d.freq = v[0];  d.ratio = v[1];  d.index = v[2];
                d.fmDecay = v[3]; d.ampDecay = v[4]; d.bend = v[5];
                d.bendDecay = v[6]; d.noise = v[7]; d.noiseFreq = v[8];
                d.noiseQ = v[9]; d.noiseDecay = v[10]; d.gain = v[11];
                pan[idx] = v[12];
                any = true;
            });
        if (!ok || !any) return false;

        for (int i = 0; i < kNumStrokes; i++) { table_[i] = tmp[i]; strokePan_[i] = pan[i]; }
        kitPath_ = path ? path : "";
        return true;
    }

    // ---- a cartridge voice on a lane ----
    //
    // A lane either strikes the kit - derived from the eleven strokes - or
    // plays one DX7 voice, struck at a different note per stroke so the lane
    // still has a high and a low in it. A lane holding a cartridge voice goes
    // through the DX engine whatever the Engine switch says, because there is
    // no such thing as a DX7 patch on the two-operator engine and silently
    // ignoring the assignment would be worse than overriding the switch.
    void setLaneVoice(int lane, const uint8_t patch[156], const char* name) {
        if (lane < 0 || lane >= kLanes || !patch) return;
        std::memcpy(laneVoice_[lane], patch, 156);
        laneUseVoice_[lane] = true;
        laneVoiceName_[lane] = name ? std::string(name) : std::string();
        laneVoiceGain_[lane] = normFor(patch);
    }

    // Brings a cartridge voice to the level the derived drums sit at. Without
    // it a sustained patch on a sixteenth grid piles up past 2.0 - measured -
    // and the target leaves headroom for exactly that stacking.
    float normFor(const uint8_t patch[156]) const {
        const float pk = dx_.patchPeak(patch, mdx::kFirstNote);
        return (pk > 0.0001f) ? std::min(1.0f, 0.55f / pk) : 1.f;
    }
    void clearLaneVoice(int lane) {
        if (lane < 0 || lane >= kLanes) return;
        laneUseVoice_[lane] = false;
        laneVoiceName_[lane].clear();
        laneVoiceGain_[lane] = 1.f;
    }
    bool laneHasVoice(int lane) const {
        return lane >= 0 && lane < kLanes && laneUseVoice_[lane];
    }
    const char* laneVoiceName(int lane) const {
        return (lane >= 0 && lane < kLanes) ? laneVoiceName_[lane].c_str() : "";
    }
    int lanesOnVoices() const {
        int n = 0;
        for (int i = 0; i < kLanes; i++) if (laneUseVoice_[i]) n++;
        return n;
    }

    // ---- per-stroke DX7 patches ----
    //
    // A stroke is normally derived from its twelve numbers every time it is
    // struck. Editing an operator has to detach it from that, or the next hit
    // would throw the edit away - so the derived bytes are materialised once
    // and then owned.

    // The bytes the patch page should draw: the edited ones if there are any,
    // otherwise the derivation as it stands right now.
    void patchFor(int stroke, uint8_t out[156]) const {
        if (stroke < 0 || stroke >= kNumStrokes) { for (int i = 0; i < 156; i++) out[i] = 0; return; }
        if (strokeEdited_[stroke]) { std::memcpy(out, strokePatch_[stroke], 156); return; }
        derivePatchFor(stroke, out);
    }
    bool strokeEdited(int stroke) const {
        return stroke >= 0 && stroke < kNumStrokes && strokeEdited_[stroke];
    }

    // Writes one patch byte, materialising the derivation first if this is the
    // first edit. `lo` and `hi` are the field's own range; DX7 fields are not
    // all 0..99 and a byte out of range makes msfa index off the end of a
    // table rather than merely sounding wrong.
    void setPatchByte(int stroke, int off, int value, int lo, int hi) {
        if (stroke < 0 || stroke >= kNumStrokes || off < 0 || off >= 156) return;
        if (!strokeEdited_[stroke]) {
            derivePatchFor(stroke, strokePatch_[stroke]);
            strokeEdited_[stroke] = true;
        }
        strokePatch_[stroke][off] = (uint8_t)clampi(value, lo, hi);
        strokeGain_[stroke] = normFor(strokePatch_[stroke]);
    }

    void revertStroke(int stroke) {
        if (stroke < 0 || stroke >= kNumStrokes) return;
        strokeEdited_[stroke] = false;
        strokeGain_[stroke] = 1.f;
    }
    void revertAllStrokes() {
        for (int i = 0; i < kNumStrokes; i++) revertStroke(i);
    }

    // for the state stream and for sets
    const uint8_t* editedBytes(int stroke) const {
        return (stroke >= 0 && stroke < kNumStrokes) ? strokePatch_[stroke] : nullptr;
    }
    void loadEdited(int stroke, const uint8_t* bytes) {
        if (stroke < 0 || stroke >= kNumStrokes || !bytes) return;
        std::memcpy(strokePatch_[stroke], bytes, 156);
        strokeEdited_[stroke] = true;
        strokeGain_[stroke] = normFor(strokePatch_[stroke]);
    }

    // Strike a stroke now, so an edit can be heard without waiting for the
    // transport to reach it. Lane 0 places it, because a stroke does not
    // belong to a lane and something has to supply a level and a pan.
    void audition(int stroke) {
        if (stroke < 0 || stroke >= kNumStrokes) return;
        trigger(0, stroke);
    }

    // One operator's own waveform, straight out of msfa.
    bool operatorWave(int stroke, int op, float* out, int n) const {
        uint8_t patch[156];
        patchFor(stroke, patch);
        return dx_.renderOperator(patch, op, mdx::kFirstNote + stroke, out, n);
    }

    void resetKit() {
        for (int i = 0; i < kNumStrokes; i++) { table_[i] = kStrokes[i]; strokePan_[i] = 0.5f; }
        kitPath_.clear();
    }
    const std::string& kitPath() const { return kitPath_; }

private:
    static int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
    static float dbGain(float db) {
        if (db <= -59.9f) return 0.f;
        return (std::fabs(db) < 1e-6f) ? 1.f : std::pow(10.f, db / 20.f);
    }
    double secPerBeat() const { return secPerBeat_; }

    void fireStep(int64_t step, int offsetInBlock) {
        int section = 0, bar = 0;
        locate(step, section, bar);
        curSection_ = section;
        curBar_ = bar;

        // Swing holds the odd 16ths back. Two thirds of a step at 120 bpm is
        // four thousand samples and a block is a few hundred, so a swung hit
        // usually belongs to a later block than the one that noticed it -
        // which is why these are queued in sample time rather than struck here.
        const double swing = clampf(p_[kSwing], 0.f, 1.f);

        for (int lane = 0; lane < kLanes; lane++) {
            const char* pat = kRhythms[rhythmOf(lane)];
            const int len = (int)std::strlen(pat);
            if (len <= 0) continue;

            int stroke = -1, idx = 0;
            const bool sounds = hitAt(step, lane, stroke, idx);

            laneStep_[lane] = idx;
            laneLen_[lane] = len;
            laneHit_[lane] = false;
            laneStroke_[lane] = pat[idx];

            if (!sounds) continue;

            int64_t at = offsetInBlock;
            if (swing > 0 && (idx % 2) == 1) {
                // up to two thirds of a step late
                const double stepSamples = sr_ * secPerBeat_ * 0.25;
                at += (int64_t)(swing * 0.66 * stepSamples);
            }
            if (pendingN_ < kMaxPending) {
                Pending& q = pending_[pendingN_++];
                q.at = at;
                q.lane = lane;
                q.stroke = stroke;
            }
            laneHit_[lane] = true;
        }
    }

    static float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

    // Voices for a stretch of the block, from wherever they have got to.
    void renderVoices(float* L, float* R, int n) {
        if (n <= 0) return;
        for (int v = 0; v < kMaxVoices; v++) {
            if (!voices_[v].active()) continue;
            voices_[v].addTo(L, R, n, voicePan_[v]);
        }
        // The DX voices are rendered whichever engine is selected, so switching
        // mid-bar lets the hits already sounding ring out instead of cutting.
        dx_.render(L, R, n);
    }

    // Few enough that an insertion sort is the cheapest thing that is also
    // obviously right.
    void sortPending() {
        for (int i = 1; i < pendingN_; i++) {
            const Pending key = pending_[i];
            int j = i - 1;
            while (j >= 0 && pending_[j].at > key.at) { pending_[j + 1] = pending_[j]; j--; }
            pending_[j + 1] = key;
        }
    }

    // Everything struck this block leaves; everything still owed moves back by
    // the length of it.
    void advancePending(int n) {
        int keep = 0;
        for (int i = 0; i < pendingN_; i++) {
            if (pending_[i].at < n) continue;
            pending_[keep] = pending_[i];
            pending_[keep].at -= n;
            keep++;
        }
        pendingN_ = keep;
    }

    struct Busy {
        std::atomic<bool>& f;
        explicit Busy(std::atomic<bool>& flag) : f(flag) { f.store(true, std::memory_order_release); }
        ~Busy() { f.store(false, std::memory_order_release); }
    };

    void silenceAll() {
        for (int v = 0; v < kMaxVoices; v++) voices_[v].silence();
        dx_.allOff();
        pendingN_ = 0;
    }

    void restore(double beats, double spb, bool running, int64_t step) {
        beats_ = beats; secPerBeat_ = spb; running_ = running; lastStep_ = step;
    }

    static int64_t lcm64(int64_t a, int64_t b) {
        if (a <= 0 || b <= 0) return std::max<int64_t>(1, std::max(a, b));
        int64_t x = a, y = b;
        while (y != 0) { const int64_t t = x % y; x = y; y = t; }
        return a / x * b;
    }

    static int strokeIndex(char c) {
        for (int i = 0; i < kNumStrokes; i++) if (kStrokeChars[i] == c) return i;
        return -1;
    }

    void trigger(int lane, int stroke) {
        const int kit = clampi((int)(p_[laneParam(lane, kLaneKit)] + 0.5f), 0, kNumKits - 1);
        const float pan = clampf(p_[laneParam(lane, kLanePan)], 0.f, 1.f);

        DrumSettings s;
        s.tune = p_[laneParam(lane, kLaneTune)];
        s.decay = p_[laneParam(lane, kLaneDecay)];
        s.drive = p_[laneParam(lane, kLaneDrive)];
        s.level = p_[laneParam(lane, kLaneLevel)];
        s.fmAmount = 1.f;
        s.noiseAmount = 1.f;
        s.tone = clampf(p_[kTone], 0.f, 1.f);

        if (dxEngine() || laneUseVoice_[lane]) { triggerDx(lane, stroke, kit, pan, s); return; }

        int slot = -1;
        for (int v = 0; v < kMaxVoices; v++)
            if (!voices_[v].active()) { slot = v; break; }
        if (slot < 0) slot = nextSteal_++ % kMaxVoices;   // steal round robin

        voices_[slot].trigger(table_[stroke], kit, 1.0f, s);
        // The kit's own pan sits under the lane's, so a kit that places its
        // strokes across the field keeps that placement when a lane is moved.
        voicePan_[slot] = clampf(pan + (strokePan_[stroke] - 0.5f), 0.f, 1.f);
    }

    // The same strike on the DX7 engine. The stroke is derived into a patch
    // per hit rather than cached, because tune and decay are lane controls and
    // a cache keyed only by stroke would play the wrong lane's settings.
    void triggerDx(int lane, int stroke, int kit, float pan, const DrumSettings& s) {
        uint8_t patch[156];
        float norm = 1.f;
        if (laneUseVoice_[lane]) {
            std::memcpy(patch, laneVoice_[lane], 156);
            norm = laneVoiceGain_[lane];
        } else if (strokeEdited_[stroke]) {
            // An edited patch is played as written. The lane's tune, decay and
            // tone scale the *derivation*, so once a patch has been edited by
            // hand they have nothing left to scale - same bargain as an
            // imported cartridge voice. Level and pan still apply, because
            // those are mix, not patch.
            std::memcpy(patch, strokePatch_[stroke], 156);
            norm = strokeGain_[stroke];
        } else {
            const KitDef& k = kKits[kit];
            mdx::DeriveSettings ds;
            ds.kitFreq = k.freq;  ds.kitDecay = k.decay;
            ds.kitNoise = k.noise; ds.kitIndex = k.index;
            ds.tune = s.tune;
            ds.decay = s.decay;
            ds.tone = s.tone;
            ds.velocity = 1.f;
            ds.velFM = clampf(p_[kVelFM], 0.f, 1.f);
            mdx::derivePatch(table_[stroke], stroke, ds, patch);
        }
        // A lane on a cartridge voice is one instrument, so it holds one note:
        // the group makes each strike release the one before it. The derived
        // drums stay ungrouped, because a drum ringing under the next hit is
        // the whole point of a drum.
        dx_.noteOn(patch, mdx::kFirstNote + stroke, 127,
                   clampf(pan + (strokePan_[stroke] - 0.5f), 0.f, 1.f),
                   s.level * norm, stroke,
                   laneUseVoice_[lane] ? lane : -1);
    }

    // The derivation as the page should see it: neutral lane settings, kit 0.
    // A lane's tune and decay are not baked in, because four lanes can strike
    // the same stroke and the page has to show one patch, not four.
    void derivePatchFor(int stroke, uint8_t out[156]) const {
        const KitDef& k = kKits[0];
        mdx::DeriveSettings ds;
        ds.kitFreq = k.freq;  ds.kitDecay = k.decay;
        ds.kitNoise = k.noise; ds.kitIndex = k.index;
        ds.tune = 0.f;
        ds.decay = 1.f;
        ds.tone = clampf(p_[kTone], 0.f, 1.f);
        ds.velocity = 1.f;
        ds.velFM = clampf(p_[kVelFM], 0.f, 1.f);
        mdx::derivePatch(table_[clampi(stroke, 0, kNumStrokes - 1)], stroke, ds, out);
    }

    void publish() {
        const int cur = viewIdx_.load(std::memory_order_relaxed);
        EngineView& v = views_[1 - cur];
        for (int i = 0; i < kLanes; i++) {
            v.lanes[i].step = laneStep_[i];
            v.lanes[i].length = laneLen_[i];
            v.lanes[i].hit = laneHit_[i];
            v.lanes[i].stroke = laneStroke_[i];
            v.lanes[i].playing = laneEnabled(curSection_, i);
        }
        v.section = curSection_;
        v.barInSection = curBar_;
        v.sectionBars = sectionBars(curSection_);
        v.running = running_;
        v.engine = dxEngine() ? 1 : 0;
        v.voices = activeVoices();
        viewIdx_.store(1 - cur, std::memory_order_release);
    }

    void reset() {
        setDefaults();
        revertAllStrokes();
        dx_.allOff();
        for (int i = 0; i < kLanes; i++) clearLaneVoice(i);
        for (int i = 0; i < kLanes; i++) {
            laneStep_[i] = 0; laneLen_[i] = 16;
            laneHit_[i] = false; laneStroke_[i] = '.';
        }
        lastStep_ = -1;
        beats_ = 0;
    }

    void setDefaults() {
        p_[kSwing] = 0.f;
        p_[kMaster] = 0.8f;
        p_[kOutput] = 0.f;
        p_[kSongOn] = 0.f;
        p_[kSongLoop] = 1.f;
        p_[kEngine] = 0.f;
        p_[kTone] = 0.5f;
        p_[kVelFM] = 0.55f;

        // four lanes on four different rhythms and kits, so it makes a groove
        // the moment it is loaded rather than one drum on its own
        const int startRhythm[kLanes] = {0, 6, 0, 12};
        const int startKit[kLanes] = {0, 1, 2, 1};
        for (int i = 0; i < kLanes; i++) {
            p_[laneParam(i, kLaneRhythm)] = (float)startRhythm[i];
            p_[laneParam(i, kLaneKit)] = (float)startKit[i];
            p_[laneParam(i, kLaneLevel)] = 0.9f;
            p_[laneParam(i, kLanePan)] = 0.5f + (i - 1.5f) * 0.12f;
            p_[laneParam(i, kLaneTune)] = 0.f;
            p_[laneParam(i, kLaneDecay)] = 1.f;
            p_[laneParam(i, kLaneDrive)] = 0.12f;
            p_[laneParam(i, kLaneMute)] = (i == 0) ? 0.f : 1.f;   // lane 1 only
        }
        for (int s = 0; s < kSections; s++) {
            p_[sectionParam(s, kSectionBars)] = (s < 2) ? 4.f : 0.f;
            for (int l = 0; l < kLanes; l++)
                p_[sectionParam(s, kSectionL1 + l)] = (s == 0 && l == 0) ? 1.f
                                                    : (s == 1 ? 1.f : 0.f);
        }
    }

    double sr_ = 44100.0;
    double secPerBeat_ = 0.5;           // 120 bpm until the host says otherwise
    double beats_ = 0;
    bool running_ = false;
    int64_t lastStep_ = -1;

    float p_[kNumParams] = {0};
    DrumVoice voices_[kMaxVoices];
    float voicePan_[kMaxVoices] = {0};
    int nextSteal_ = 0;

    mdx::DxEngine dx_;
    // The stroke table the lanes actually play. It starts as the module's own
    // and is replaced wholesale by a loaded kit, so both engines are driven
    // from one edited set of strokes rather than each from its own.
    StrokeDef table_[kNumStrokes];
    uint8_t laneVoice_[kLanes][156] = {{0}};
    bool laneUseVoice_[kLanes] = {false};
    float laneVoiceGain_[kLanes] = {1.f, 1.f, 1.f, 1.f};
    std::string laneVoiceName_[kLanes];
    float strokePan_[kNumStrokes] = {0};
    uint8_t strokePatch_[kNumStrokes][156] = {{0}};
    bool strokeEdited_[kNumStrokes] = {false};
    float strokeGain_[kNumStrokes] = {1.f};
    std::string kitPath_;

    int laneStep_[kLanes] = {0};
    int laneLen_[kLanes] = {16};
    bool laneHit_[kLanes] = {false};
    char laneStroke_[kLanes] = {'.'};
    int curSection_ = 0, curBar_ = 0;

    EngineView views_[2];
    Fired fired_[kMaxFiredPerBlock];
    int firedN_ = 0;

    // Hits owed, in samples from the start of the current block. A swung
    // sixteenth is usually owed to a block that has not happened yet.
    struct Pending { int64_t at = 0; int lane = 0; int stroke = 0; };
    static const int kMaxPending = 64;
    // The DX engine's chunk, in samples: see renderInto for why hits land on it.
    static const int kTriggerGrid = 64;
    Pending pending_[kMaxPending];
    int pendingN_ = 0;
    std::atomic<bool> busy_ {false};
    std::atomic<int> viewIdx_{0};
};

} // namespace mseq
