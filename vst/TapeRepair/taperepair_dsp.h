// TapeRepair - record a transfer, mend it, play it back.
//
// Not a realtime effect, on purpose. Everything worth doing to a tape transfer
// wants the whole recording in hand: the hum notch runs forwards and then
// backwards so it adds no phase shift, and the hiss floor is measured from a
// stretch of this recording rather than guessed at from what has just gone
// past. A device that processes three milliseconds at a time can do neither.
//
// So it works the way a tape machine does. Arm it, play the side in, and it
// keeps the take. Press LEARN on a gap and it measures the floor. Press REPAIR
// and it mends the take offline. Then it plays back, and A/B swaps between what
// came off the tape and what came out of the repair.
//
// The heavy work happens on a worker thread - never the audio one, and no
// longer the editor's either. While it runs, `busy_` is set and the audio
// callback passes its input through untouched, which is the honest thing for a
// device to do when the buffer it would otherwise be reading is being
// rewritten underneath it.
//
// It ran on the editor's thread until a repair took the plug-in down. Bitwig
// logged all three plug-ins in its shared host process as crashed, five and a
// half seconds after REPAIR was pressed, and the core showed SIGABRT by tkill
// rather than a fault: the host had asked the plug-in window a question, got
// no answer while a three minute take was being denoised, and killed the
// process for hanging. A side of tape is half a minute of that work. No amount
// of care inside the algorithms fixes a design that does them on the thread
// the host is waiting on.

#pragma once

#include "tape_restore.h"

#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <thread>
#include <vector>

namespace trep {

enum Param {
    kHum = 0,        // 0 off, 1 = 50 Hz, 2 = 60 Hz
    kHarmonics,      // how many multiples of it to notch
    kDenoise,        // how many multiples of the measured floor to subtract
    kFloor,          // dB; the most any one bin may be pulled down
    kDcBlock,
    kNormalize,      // target peak in dB; below kNormOff it is left alone
    kCompare,        // 0 shows the repair, 1 shows the tape
    kOutput,         // dB
    kBypass,
    kNumParams
};

static const float kNormOff = -60.f;
static const int   kMaxTakeSeconds = 60 * 50;      // a C90 side and then some

// What the run-out sounds like: quieter than this, for this long. A tape that
// has finished is not silent - there is still head noise - so the threshold is
// well above digital zero, and ten seconds is longer than any gap between two
// songs on a side.
static const float  kRunOutDb      = -60.f;
static const double kRunOutSeconds = 10.0;
static const double kRunOutKeep    = 1.0;   // tail left on, so nothing is clipped

class TapeRepair {
public:
    TapeRepair() { setDefaults(); }

    void setSampleRate(double sr) {
        sr_ = (sr > 0) ? sr : 48000.0;
        take_.sampleRate = sr_;
        fixed_.sampleRate = sr_;
        clearTake();
    }

    void setParam(int id, float v) { if (id >= 0 && id < kNumParams) p_[id] = v; }
    float param(int id) const { return (id >= 0 && id < kNumParams) ? p_[id] : 0.f; }

    // ---------------------------------------------------------- transport
    void arm(bool on) {
        if (on == armed_) return;
        if (on) { clearTake(); playing_ = false; silent_ = 0; }
        armed_ = on;
    }
    bool armed() const { return armed_; }

    // ---------------------------------------------------------- unattended
    //
    // Two ways to record a side without standing over the machine.
    //
    // FOLLOW arms with the host's transport, so pressing play in the scene
    // starts the take and stopping ends it - which is also the answer to
    // getting what is already on a track into here: play it at this.
    //
    // AUTO-STOP watches for the run-out. Ten seconds under -60 dB and the take
    // ends itself, with the silence it just recorded dropped. A C90 side is
    // forty-five minutes and nobody should have to sit through it twice, once
    // to play it and once to find where it stopped.
    void setFollowHost(bool on) { follow_ = on; }
    void setAutoStop(bool on)   { autoStop_ = on; if (!on) silent_ = 0; }
    bool followHost() const { return follow_; }
    bool autoStop() const   { return autoStop_; }

    void setHostPlaying(bool playing) {
        if (playing == hostPlaying_) return;
        hostPlaying_ = playing;
        if (follow_) arm(playing);
    }

    // Loudest thing on the input lately, for a level check before the tape
    // rolls. Decays rather than reporting whatever the last block held, so a
    // meter reading it at its own pace sees something steady.
    float inputPeak() const { return inPeak_.load(std::memory_order_relaxed); }

    void play(bool on) {
        if (on && !hasTake()) return;
        playing_ = on;
        if (on) readPos_ = 0;
    }
    bool playing() const { return playing_; }

    void clearTake() {
        take_.ch.assign(2, {});
        fixed_.ch.assign(2, {});
        profile_ = trestore::NoiseProfile{};
        readPos_ = 0;
        playing_ = false;
    }

    // Installs a clip as the take, as though it had been recorded. Anything
    // learned or repaired about the previous one is dropped: a profile
    // measured from a different recording is worse than none.
    //
    // Folded to two channels because that is what the transport plays. A mono
    // file is doubled rather than left half silent, and anything wider is
    // taken from its first two - a repair is not the place to guess at a
    // downmix.
    bool setTake(std::vector<std::vector<float>> channels) {
        if (channels.empty() || channels[0].empty()) return false;
        if (working()) return false;      // a repair is reading the take
        Guard g(busy_);
        playing_ = false;
        readPos_ = 0;
        fixed_.ch.assign(2, {});
        profile_ = trestore::NoiseProfile{};

        if (channels.size() == 1) channels.push_back(channels[0]);
        channels.resize(2);
        if (channels[1].size() != channels[0].size()) channels[1] = channels[0];

        take_.ch = std::move(channels);
        take_.sampleRate = sr_;
        return true;
    }

    bool hasTake() const { return !take_.ch.empty() && !take_.ch[0].empty(); }
    bool hasRepair() const { return !fixed_.ch.empty() && !fixed_.ch[0].empty(); }
    bool hasProfile() const { return profile_.valid(); }
    double takeSeconds() const { return take_.ch.empty() ? 0.0 : (double)take_.ch[0].size() / sr_; }
    double playSeconds() const { return (double)readPos_ / sr_; }

    const trestore::Clip& take() const { return take_; }
    const trestore::Clip& repaired() const { return fixed_; }

    // Whichever buffer A/B is showing, falling back to the take when there is
    // no repair yet - so PLAY does something sensible before REPAIR is pressed.
    const trestore::Clip& audible() const {
        return (p_[kCompare] < 0.5f && hasRepair()) ? fixed_ : take_;
    }

    // ------------------------------------------------------ offline, worker
    //
    // Started rather than done: this returns as soon as the worker has the
    // job, and the editor watches working() and progress() from its timer.
    // Everything the worker touches - the take, the profile, the repair - is
    // off limits to the other threads until working() goes false again, which
    // is what busy_ tells the audio callback.
    bool startRepair() {
        if (!hasTake() || working()) return false;
        wait();                                   // reap the previous worker
        cancel_.store(false, std::memory_order_release);
        progress_.store(0.0, std::memory_order_relaxed);
        // Set here rather than in the thread, so the audio callback is already
        // passing through by the time the first block after the click lands.
        busy_.store(true, std::memory_order_release);
        worker_ = std::thread([this] {
            runRepair();
            progress_.store(1.0, std::memory_order_relaxed);
            busy_.store(false, std::memory_order_release);
        });
        return true;
    }

    bool working() const { return busy_.load(std::memory_order_acquire); }
    double progress() const { return progress_.load(std::memory_order_relaxed); }

    // Asks the worker to stop. It checks between windows of the denoise, so a
    // cancel lands within a few milliseconds; the half-finished copy is thrown
    // away and the take is exactly as it was.
    void cancel() { cancel_.store(true, std::memory_order_release); }

    // Blocks until the worker is done. The destructor calls it, and so must
    // anything about to free what the worker is reading.
    void wait() { if (worker_.joinable()) worker_.join(); }

    ~TapeRepair() { cancel(); wait(); }

    // ------------------------------------------------- offline, this thread
    //
    // Measures the noise floor. With no hint it finds the quietest second of
    // the take by itself, which is nearly always a gap between phrases or the
    // run-in before the music.
    bool learn(double atSec = -1.0, double lenSec = 1.0) {
        if (!hasTake()) return false;
        Guard g(busy_);
        profile_ = (atSec < 0.0) ? trestore::measureQuietest(take_, lenSec)
                                 : trestore::measureNoise(take_, atSec, lenSec);
        return profile_.valid();
    }

    // Runs the chain on a copy of the take, so the tape is never overwritten
    // and REPAIR can be pressed again with different settings.
    // The same work, on whichever thread calls it. Kept because the tests
    // drive it directly and want the answer before the next line runs; a host
    // should call startRepair() instead.
    bool repair() {
        if (!hasTake() || working()) return false;
        Guard g(busy_);
        return runRepair();
    }

    void revert() {
        if (working()) return;
        Guard g(busy_);
        fixed_.ch.assign(2, {});
    }

    // ------------------------------------------------------------- audio
    void processStereo(const float* inL, const float* inR,
                       float* outL, float* outR, int n) {
        if (n <= 0) return;

        // Bypassed, or the buffers are being rewritten on another thread:
        // hand the input straight back rather than read what is moving.
        if (p_[kBypass] >= 0.5f || busy_.load(std::memory_order_acquire)) {
            for (int i = 0; i < n; i++) { outL[i] = inL[i]; outR[i] = inR[i]; }
            return;
        }

        const float g = dbGain(p_[kOutput]);

        // Measured whether or not anything is being recorded: the point of a
        // level check is to make it before pressing ARM.
        float peak = 0.f;
        for (int i = 0; i < n; i++)
            peak = std::max(peak, std::max(std::fabs(inL[i]), std::fabs(inR[i])));
        float shown = inPeak_.load(std::memory_order_relaxed) * 0.85f;
        inPeak_.store(std::max(shown, peak), std::memory_order_relaxed);

        if (armed_) {
            // Recording. Monitor what is going down, and stop growing rather
            // than eat the machine if someone leaves it armed.
            const size_t cap = (size_t)(kMaxTakeSeconds * sr_);
            for (int i = 0; i < n; i++) {
                if (take_.ch[0].size() < cap) {
                    take_.ch[0].push_back(inL[i]);
                    take_.ch[1].push_back(inR[i]);
                }
                outL[i] = inL[i] * g;
                outR[i] = inR[i] * g;
            }

            if (autoStop_) {
                // Not dbGain(): that reads -60 as "off" and returns a flat
                // zero, which no peak is ever below - so the run-out was never
                // found and the take ran to the cap. The threshold here is a
                // level, not a fader position.
                const float quiet = std::pow(10.f, kRunOutDb / 20.f);
                if (peak < quiet) silent_ += (size_t)n; else silent_ = 0;
                if (silent_ > (size_t)(kRunOutSeconds * sr_)) stopAtRunOut();
            }
            return;
        }

        if (playing_) {
            const trestore::Clip& src = audible();
            const size_t len = src.ch.empty() ? 0 : src.ch[0].size();
            for (int i = 0; i < n; i++) {
                if (readPos_ < len) {
                    outL[i] = src.ch[0][readPos_] * g;
                    outR[i] = src.ch[1][readPos_] * g;
                    readPos_++;
                } else {
                    outL[i] = 0.f;
                    outR[i] = 0.f;
                    playing_ = false;      // ran off the end
                }
            }
            return;
        }

        for (int i = 0; i < n; i++) { outL[i] = inL[i] * g; outR[i] = inR[i] * g; }
    }

    // --------------------------------------------------------- the editor
    //
    // Min and max per column, so a waveform drawn from it shows the shape of
    // the recording rather than whatever the sample nearest each pixel did.
    struct Peaks { std::vector<float> lo, hi; };

    Peaks peaks(int columns, bool ofRepair) const {
        Peaks out;
        const trestore::Clip& src = ofRepair ? fixed_ : take_;
        if (columns <= 0 || src.ch.empty() || src.ch[0].empty()) return out;

        out.lo.assign((size_t)columns, 0.f);
        out.hi.assign((size_t)columns, 0.f);
        const size_t len = src.ch[0].size();
        for (int c = 0; c < columns; c++) {
            const size_t a = (size_t)((double)c / columns * len);
            const size_t b = std::min(len, (size_t)((double)(c + 1) / columns * len));
            float lo = 0.f, hi = 0.f;
            for (size_t i = a; i < b; i++)
                for (const auto& ch : src.ch) { lo = std::min(lo, ch[i]); hi = std::max(hi, ch[i]); }
            out.lo[(size_t)c] = lo;
            out.hi[(size_t)c] = hi;
        }
        return out;
    }

    // The songs on the side, measured from whatever A/B is currently showing -
    // so splitting a repaired take gives repaired songs, and splitting before
    // REPAIR gives what came off the tape.
    std::vector<trestore::Track> tracks(const trestore::TrackOptions& o = {}) const {
        return trestore::findTracks(audible(), o);
    }

    // What the repair actually did, for the readout: how far the noise floor
    // moved. Zero when there is nothing to compare.
    double floorChangeDb() const {
        if (!hasTake() || !hasRepair()) return 0.0;
        return trestore::linearToDb(quietRms(fixed_)) - trestore::linearToDb(quietRms(take_));
    }

private:
    // Ends a take at the run-out, and drops the run-out with it.
    //
    // A second is left on, because the thing that saves you the wait should
    // not be the thing that clips the last note. Shrinking a vector frees
    // nothing and allocates nothing, so this is safe on the audio thread.
    void stopAtRunOut() {
        const size_t len = take_.ch.empty() ? 0 : take_.ch[0].size();
        const size_t keep = (size_t)(kRunOutKeep * sr_);
        if (len > silent_ + keep) {
            take_.ch[0].resize(len - silent_ + keep);
            take_.ch[1].resize(len - silent_ + keep);
        }
        armed_ = false;
        silent_ = 0;
    }

    // The chain itself, in the order the header argues for. Runs on the
    // worker under startRepair(), or on the caller's thread under repair();
    // busy_ is already set either way, so it never has to think about which.
    bool runRepair() {
        trestore::Clip work = take_;

        if (p_[kDcBlock] >= 0.5f) trestore::removeDcOffset(work);

        const int humSel = (int)std::lround(p_[kHum]);
        if (humSel > 0) {
            trestore::HumOptions h;
            h.baseHz = (humSel == 2) ? 60.0 : 50.0;
            h.harmonics = std::max(1, (int)std::lround(p_[kHarmonics]));
            trestore::removeHum(work, h);
        }

        if (p_[kDenoise] > 0.001f && profile_.valid()) {
            trestore::HissOptions o;
            o.amount = p_[kDenoise];
            o.floorDb = p_[kFloor];
            // The denoise is nineteen twentieths of the work, so it carries
            // the whole of the progress bar and all of the cancelling.
            const bool finished = trestore::reduceHiss(work, profile_, o,
                [this](double f) {
                    progress_.store(0.02 + 0.96 * f, std::memory_order_relaxed);
                    return !cancel_.load(std::memory_order_acquire);
                });
            // Cancelled: `work` is a half-mended copy, so it is dropped and
            // the take stands as it was recorded.
            if (!finished) return false;
        }

        if (p_[kNormalize] > kNormOff) trestore::normalize(work, p_[kNormalize]);

        fixed_ = std::move(work);
        return true;
    }

    struct Guard {
        std::atomic<bool>& f;
        explicit Guard(std::atomic<bool>& flag) : f(flag) { f.store(true, std::memory_order_release); }
        ~Guard() { f.store(false, std::memory_order_release); }
    };

    static float dbGain(float db) {
        if (db <= -59.9f) return 0.f;
        return (std::fabs(db) < 1e-6f) ? 1.f : std::pow(10.f, db / 20.f);
    }

    // RMS of the quietest tenth of the clip - a stand-in for its noise floor
    // that does not need the profile to still be around.
    static double quietRms(const trestore::Clip& c) {
        if (c.ch.empty() || c.ch[0].empty()) return 0.0;
        const size_t win = std::max<size_t>(1, c.ch[0].size() / 200);
        std::vector<double> vals;
        for (size_t pos = 0; pos + win <= c.ch[0].size(); pos += win) {
            double s = 0;
            for (size_t i = pos; i < pos + win; i++) s += (double)c.ch[0][i] * c.ch[0][i];
            vals.push_back(std::sqrt(s / (double)win));
        }
        if (vals.empty()) return 0.0;
        std::sort(vals.begin(), vals.end());
        return vals[vals.size() / 10];
    }

    void setDefaults() {
        p_[kHum] = 1.f;            // 50 Hz: this is not the Americas
        p_[kHarmonics] = 4.f;
        p_[kDenoise] = 1.f;
        p_[kFloor] = -18.f;
        p_[kDcBlock] = 1.f;
        p_[kNormalize] = -1.f;
        p_[kCompare] = 0.f;
        p_[kOutput] = 0.f;
        p_[kBypass] = 0.f;
    }

    double sr_ = 48000.0;
    float p_[kNumParams] = {0};
    trestore::Clip take_, fixed_;
    trestore::NoiseProfile profile_;
    size_t readPos_ = 0;
    bool armed_ = false;
    bool playing_ = false;
    bool follow_ = false, autoStop_ = false, hostPlaying_ = false;
    size_t silent_ = 0;                  // samples of run-out seen so far
    std::atomic<float> inPeak_ {0.f};
    std::atomic<bool> busy_ {false};
    std::atomic<bool> cancel_ {false};
    std::atomic<double> progress_ {0.0};
    std::thread worker_;
};

} // namespace trep
