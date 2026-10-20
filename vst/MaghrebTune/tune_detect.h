// Pitch detection, for a single voice.
//
// YIN, in the form Cheveigné and Kawahara describe: the squared difference
// function, then a cumulative mean normalisation that removes the zero-lag
// peak, then the first dip below a threshold rather than the global minimum.
// Taking the global minimum is what makes naive autocorrelation jump an
// octave down on a rich voice, which for this plug-in would be fatal - an
// octave error puts the correction a whole ṭabʿ away from where the singer
// was, and it would sound like the effect had decided to sing something else.
//
// Framework-free, so its accuracy can be measured against synthesised tones
// rather than judged by ear.

#pragma once
#include <cmath>
#include <vector>
#include <algorithm>

namespace mtune {

struct Pitch {
    double hz = 0.0;          // 0 when nothing periodic was found
    double confidence = 0.0;  // 1 - aperiodicity; 1 is a clean periodic signal
    bool voiced = false;
};

class Detector {
public:
    // Window has to hold two periods of the lowest note wanted. 2048 at 44.1k
    // reaches down to about 43 Hz, which is below any voice.
    void setup(double sampleRate, int window = 2048) {
        sr_ = (sampleRate > 0) ? sampleRate : 44100.0;
        win_ = std::max(512, window);
        maxTau_ = win_ / 2;
        diff_.assign(maxTau_, 0.0);
        cmnd_.assign(maxTau_, 0.0);
        tracking_ = false;
        held_ = 0;
        lastTau_ = 0.0;
        coast_ = 0;
    }

    void setRange(double loHz, double hiHz) {
        loHz_ = std::max(20.0, loHz);
        hiHz_ = std::max(loHz_ + 1.0, hiHz);
    }
    // Two thresholds, not one.
    //
    // A single threshold has to be strict enough not to find a pitch in noise
    // and lax enough not to let go halfway through a word, and on a real voice
    // there is no value that is both. Measured on a breathy phrase, the frames
    // this dropped sat at a confidence of 0.65 while the threshold demanded
    // 0.88 - they were not marginal, they were being refused by a number
    // chosen for synthetic tones.
    //
    // So: `enter` acquires a note and is strict, `release` holds one already
    // being tracked and is lax. Between them the tracker stays on a note
    // through breath and consonants without ever starting on one in silence.
    void setThreshold(double t) {
        enter_ = std::max(0.01, std::min(0.5, t));
        release_ = std::min(0.75, enter_ * 3.2);
    }
    void setRelease(double t) { release_ = std::max(enter_, std::min(0.9, t)); }
    // How long it may hold without ever re-acquiring, in analysis frames.
    void setHold(int frames) {
        maxHold_ = std::max(0, frames);
        maxCoast_ = std::max(4, frames / 3);
    }
    void resetTracking() { tracking_ = false; held_ = 0; lastTau_ = 0.0; coast_ = 0; }

    int window() const { return win_; }

    // `x` must hold at least window() samples. Not const: the voiced decision
    // depends on whether a note was already being tracked.
    Pitch analyse(const float* x, int n) {
        Pitch out;
        if (!x || n < win_ || diff_.empty()) return out;

        const int tauMin = std::max(2, (int)std::floor(sr_ / hiHz_));
        const int tauMax = std::min(maxTau_ - 1, (int)std::ceil(sr_ / loHz_));
        if (tauMax <= tauMin) return out;

        // Too quiet to be a note. Without this the detector happily finds a
        // pitch in room noise and the correction chatters between silences.
        double energy = 0.0;
        for (int i = 0; i < win_; i++) energy += (double)x[i] * x[i];
        if (energy / win_ < 1e-7) {
            tracking_ = false; held_ = 0; lastTau_ = 0; coast_ = 0;
            return out;
        }

        // ---- squared difference ----
        std::vector<double>& d = diff_;
        std::vector<double>& c = cmnd_;
        for (int tau = tauMin; tau <= tauMax; tau++) {
            double sum = 0.0;
            const int lim = win_ - tau;
            for (int i = 0; i < lim; i++) {
                const double delta = (double)x[i] - (double)x[i + tau];
                sum += delta * delta;
            }
            d[tau] = sum;
        }

        // ---- cumulative mean normalisation ----
        //
        // This is the step that separates YIN from autocorrelation: dividing
        // by the running mean makes d'(tau) about 1 for a wrong lag and near
        // 0 for the right one, so a fixed threshold means the same thing on
        // any signal.
        c[tauMin] = 1.0;
        double running = 0.0;
        for (int tau = tauMin; tau <= tauMax; tau++) {
            running += d[tau];
            c[tau] = (running > 0.0) ? d[tau] * (tau - tauMin + 1) / running : 1.0;
        }

        // ---- the first dip below a threshold, not the deepest ----
        //
        // Taking the deepest dip is what makes autocorrelation drop an octave;
        // taking the first one that is convincing enough is what YIN is for.
        // The bar is `enter_` when acquiring and `release_` when already on a
        // note.
        int best = -1;
        bool heldOn = false;

        // While already on a note, look first at lags near the one being
        // tracked. A lax bar applied to the whole range finds the dip at half
        // the period just as happily as the real one, and reports the note an
        // octave up - which is how the first attempt at this traded fourteen
        // octave errors for two percent of holding. Continuity first, then the
        // strict search.
        // Coasting counts as still being on the note. Without this a single
        // bad frame demoted the tracker to the strict bar, and on a signal
        // hovering around d' = 0.38 it then could not get back until a clean
        // frame happened along - so one dropout became a run of them.
        if ((tracking_ || coast_ > 0) && lastTau_ > 0) {
            const int lo = std::max(tauMin + 1, (int)(lastTau_ * 0.80));
            const int hi = std::min(tauMax - 1, (int)(lastTau_ * 1.25));
            double bestV = release_;
            for (int tau = lo; tau <= hi; tau++)
                if (c[tau] < bestV) { bestV = c[tau]; best = tau; }
            if (best > 0) {
                while (best + 1 < tauMax && c[best + 1] < c[best]) best++;
                heldOn = true;
            }
        }

        if (best < 0) {
            for (int tau = tauMin + 1; tau < tauMax; tau++) {
                if (c[tau] < enter_) {
                    while (tau + 1 < tauMax && c[tau + 1] < c[tau]) tau++;
                    best = tau;
                    break;
                }
            }
        }

        if (best >= 0) {
            out.confidence = std::max(0.0, 1.0 - c[best]);
            out.voiced = true;
            // Only a dip good enough to have acquired the note counts as
            // re-acquiring it; holding on the lax bar runs the hold budget down
            // so a tracker cannot cling to a note forever.
            if (!heldOn || c[best] < enter_) held_ = 0; else held_++;
            if (held_ > maxHold_) {
                out.voiced = false; tracking_ = false; held_ = 0; lastTau_ = 0; coast_ = 0;
            } else {
                tracking_ = true;
                coast_ = 0;
            }
        } else {
            double lo = 1e30;
            for (int tau = tauMin + 1; tau < tauMax; tau++)
                if (c[tau] < lo) { lo = c[tau]; best = tau; }
            if (best < 0) { tracking_ = false; return out; }
            out.confidence = std::max(0.0, 1.0 - lo);
            out.voiced = false;
            tracking_ = false;
            held_ = 0;
            // keep the lag for a little while, so a moment of breath does not
            // throw away the note the singer is still on
            if (coast_ < maxCoast_) coast_++; else lastTau_ = 0;
        }

        // ---- parabolic interpolation ----
        //
        // A whole-sample lag is only accurate to about 8 cents at 400 Hz,
        // which is a third of a neutral interval's distinctiveness. Fitting a
        // parabola through the dip takes that to well under a cent.
        double tau = best;
        if (best > 0 && best + 1 < (int)c.size()) {
            const double a = c[best - 1], b = c[best], g = c[best + 1];
            const double denom = 2.0 * (2.0 * b - a - g);
            if (std::fabs(denom) > 1e-12) tau = best + (g - a) / denom;
        }
        if (tau <= 0) return out;

        out.hz = sr_ / tau;
        if (out.hz < loHz_ || out.hz > hiHz_) { out.voiced = false; tracking_ = false; }
        if (out.voiced) lastTau_ = tau;
        return out;
    }

private:
    double sr_ = 44100.0;
    int win_ = 2048;
    int maxTau_ = 1024;
    double loHz_ = 65.0, hiHz_ = 1200.0;
    double enter_ = 0.15, release_ = 0.48;
    int maxHold_ = 40, held_ = 0;        // ~230 ms at a 256 sample hop
    double lastTau_ = 0.0;               // the lag being tracked, for continuity
    int coast_ = 0, maxCoast_ = 12;      // frames a lost note is still remembered
    bool tracking_ = false;
    std::vector<double> diff_, cmnd_;
};

} // namespace mtune
