// Pitch shifting, time-domain and pitch-synchronous.
//
// TD-PSOLA: cut the input into grains centred on the glottal pulses, then lay
// them back down at a different spacing. The grain keeps the waveform shape of
// the original, and it is the shape that carries the formants - so the vowel
// survives and only the rate of repetition changes. A phase vocoder would be
// easier to write and would make every corrected note sound like it had been
// through a pipe.
//
// Two details do most of the work:
//
//   - marks are placed on the largest peak near each expected period, not
//     every T samples blindly. Grains that start at the same point of the
//     cycle add coherently; grains that do not sound hollow and phasey.
//
//   - the window sum is accumulated alongside the output and divided out.
//     Shifting up puts the grains closer together than the window was built
//     for, and without this the voice gets louder as it goes sharp.
//
// Framework-free, so the shift can be measured rather than trusted.

#pragma once
#include <cmath>
#include <vector>
#include <algorithm>
#include <deque>

namespace mtune {

class Shifter {
public:
    // `lowestHz` is the lowest note the detector will report. The grain is
    // sized from it, and latency follows from the grain, so the two must be
    // the same number - a shifter carrying grains for a 55 Hz note while the
    // detector never looks below 70 Hz is paying 300 samples of latency per
    // block for a note that cannot arrive.
    void setup(double sampleRate, double lowestHz = 70.0) {
        sr_ = (sampleRate > 0) ? sampleRate : 44100.0;
        maxPeriod_ = sr_ / ((lowestHz > 20.0) ? lowestHz : 20.0);
        lookahead_ = 2.0 * maxPeriod_ + kMaxBlock;
        size_ = 1 << 15;                       // ~0.74 s at 44.1k, ample
        mask_ = size_ - 1;
        in_.assign(size_, 0.f);
        lp_.assign(size_, 0.f);
        acc_.assign(size_, 0.f);
        win_.assign(size_, 0.f);
        reset();
    }

    void reset() {
        std::fill(in_.begin(), in_.end(), 0.f);
        std::fill(lp_.begin(), lp_.end(), 0.f);
        z1_ = z2_ = 0.0;
        std::fill(acc_.begin(), acc_.end(), 0.f);
        std::fill(win_.begin(), win_.end(), 0.f);
        marks_.clear();
        // Start the write head a whole buffer in, so the read head - which
        // trails it by one grain - never has to be clamped at zero. Clamping
        // it collapsed the latency to one block, and then every grain failed
        // the "is it inside what we have received" test and nothing was laid
        // down at all.
        wpos_ = size_; readPos_ = 0; outPos_ = 0.0; lastMark_ = -1;
        period_ = 220.0;
        primed_ = false;
    }

    // The detector's period, in samples. Held between calls, so an unvoiced
    // stretch keeps cutting at the last known rate rather than stalling.
    void setPeriod(double samples) {
        if (samples > 8.0 && samples < maxPeriod_) period_ = samples;
    }
    // The lowest note the grain has to hold, changed while running.
    //
    // Latency is two of those periods, so this is the one dial that trades
    // range against delay: 70 Hz carries any voice and costs 31 ms at 48k,
    // 100 Hz costs 23 and stops following a voice below G#2. The buffers were
    // sized for the longest grain at setup, so nothing is reallocated - but
    // the grain layout changes underneath the overlap that is already laid
    // down, so what is in flight is dropped rather than half-shifted.
    void setLowest(double lowestHz) {
        const double period = sr_ / ((lowestHz > 20.0) ? lowestHz : 20.0);
        if (std::fabs(period - maxPeriod_) < 0.5) return;
        maxPeriod_ = period;
        lookahead_ = 2.0 * maxPeriod_ + kMaxBlock;
        reset();
    }

    void setRatio(double r) { ratio_ = std::max(0.25, std::min(4.0, r)); }
    double ratio() const { return ratio_; }
    double periodNow() const { return period_; }

    // Latency has to cover a whole grain each way, plus the block.
    //
    // A grain centred at C spans [C-T, C+T]. To fill an output block the
    // emitter lays grains centred as far ahead as readPos + n + T, and that
    // grain reaches forward T again - so the input must already extend
    // n + 2T beyond the read position.
    //
    // This used to be one T. For a soprano that is plenty and nothing showed;
    // for a low voice the grain could not be laid, emit() gave up, and those
    // samples were left with no window sum at all - which the divide below
    // turns into silence. The result was an output that stuttered in and out
    // several times a second on anything under about 110 Hz.
    int latency() const { return (int)lookahead_; }

    void process(const float* input, float* output, int n) {
        if (in_.empty() || n <= 0) return;

        // The lookahead is sized for a block of at most kMaxBlock. A host that
        // asks for more is served in pieces rather than starved - the same
        // starvation that used to silence low notes.
        while (n > (int)kMaxBlock) {
            process(input, output, (int)kMaxBlock);
            input  += (int)kMaxBlock;
            output += (int)kMaxBlock;
            n      -= (int)kMaxBlock;
        }

        // A copy with the harmonics taken off, for the mark finder only.
        //
        // Marks want the glottal pulse, which is a fundamental-rate event. The
        // raw waveform of a sung vowel has several comparable peaks per cycle
        // and which one is largest changes with the vowel, so picking the
        // largest raw peak puts consecutive marks at different points of the
        // cycle. Two poles at about F0 leave one peak per period to find.
        const double fc = sr_ / std::max(8.0, period_);
        const double a = 1.0 - std::exp(-2.0 * M_PI * fc / sr_);
        for (int i = 0; i < n; i++) {
            const int k = (int)((wpos_ + i) & mask_);
            in_[k] = input[i];
            z1_ += a * (input[i] - z1_);
            z2_ += a * (z1_ - z2_);
            lp_[k] = (float)z2_;
        }
        wpos_ += n;

        if (!primed_) {
            // start the output far enough behind that a full grain always fits
            readPos_ = wpos_ - (long long)lookahead_;
            outPos_ = (double)readPos_ + period_;
            primed_ = true;
        }

        findMarks();
        emit(n);

        for (int i = 0; i < n; i++) {
            const long long at = readPos_ + i;
            const int k = (int)(at & mask_);
            const float w = win_[k];
            output[i] = (w > 1e-4f) ? acc_[k] / w : 0.f;
            acc_[k] = 0.f;
            win_[k] = 0.f;
        }
        readPos_ += n;

        while (!marks_.empty() && marks_.front() < readPos_ - (long long)maxPeriod_ * 2)
            marks_.pop_front();
    }

private:
    // Larger blocks are split, so this only buys latency, not correctness.
    static constexpr double kMaxBlock = 128.0;

    // Marks go on the biggest peak of the low-passed copy within a quarter
    // period of where the next one is due. Searching a window rather than
    // trusting the period is what keeps the grains in phase when the singer
    // moves; searching the low-passed copy rather than the raw one is what
    // stops a harmonic winning the search and cutting the next grain at a
    // different point of the cycle.
    void findMarks() {
        const long long safe = wpos_ - 8;
        if (lastMark_ < 0) {
            lastMark_ = readPos_;
            marks_.push_back(lastMark_);
        }
        while (lastMark_ + (long long)period_ < safe) {
            const long long want = lastMark_ + (long long)period_;
            const int search = std::max(2, (int)(period_ * 0.25));
            long long best = want;
            float bestV = -1.f;
            for (long long p = want - search; p <= want + search; p++) {
                if (p <= lastMark_ || p >= safe) continue;
                const float v = lp_[(int)(p & mask_)];
                if (v > bestV) { bestV = v; best = p; }
            }
            lastMark_ = best;
            marks_.push_back(best);
        }
    }

    const long long* nearestMark(long long at) const {
        if (marks_.empty()) return nullptr;
        const long long* best = &marks_[0];
        long long bd = std::llabs(marks_[0] - at);
        for (size_t i = 1; i < marks_.size(); i++) {
            const long long d = std::llabs(marks_[i] - at);
            if (d < bd) { bd = d; best = &marks_[i]; }
        }
        return best;
    }

    void emit(int n) {
        const double until = (double)(readPos_ + n) + period_;
        const int guard = 0;
        int laid = 0;
        while (outPos_ < until && laid < 512) {
            const int T = (int)std::lround(period_);
            if (T < 8) break;

            // never write behind what has already gone out
            if (outPos_ - T < (double)readPos_) { outPos_ = (double)readPos_ + T; continue; }

            const long long here = (long long)std::llround(outPos_);
            const long long* m = nearestMark(here);
            if (!m) break;
            const long long centre = *m;

            // the grain has to be inside what we have actually received
            if (centre + T >= wpos_ - guard) break;

            for (int i = -T; i <= T; i++) {
                const long long src = centre + i;
                const long long dst = here + i;
                if (src < 0 || src >= wpos_) continue;
                if (dst < readPos_) continue;
                // Hann across the whole grain, so consecutive grains at the
                // analysis period sum flat
                const float w = 0.5f * (1.f + std::cos(3.14159265358979f * i / T));
                acc_[(int)(dst & mask_)] += in_[(int)(src & mask_)] * w;
                win_[(int)(dst & mask_)] += w;
            }

            // Kept fractional. Rounding the hop to a whole sample put the
            // output period at 221 instead of 220.5 on a 200 Hz note, which is
            // a flat four cents - small, but this plug-in exists to place
            // notes to the cent, so it is not small enough.
            outPos_ += period_ / ratio_;
            laid++;
        }
    }

    double sr_ = 44100.0;
    int size_ = 0, mask_ = 0;
    std::vector<float> in_, lp_, acc_, win_;
    double z1_ = 0.0, z2_ = 0.0;
    std::deque<long long> marks_;
    long long wpos_ = 0, readPos_ = 0, lastMark_ = -1;
    double outPos_ = 0.0;
    double period_ = 220.0, ratio_ = 1.0;
    double maxPeriod_ = 630.0, lookahead_ = 2.0 * 630.0 + kMaxBlock;
    bool primed_ = false;
};

} // namespace mtune
