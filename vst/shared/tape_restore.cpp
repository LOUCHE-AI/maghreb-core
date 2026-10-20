#include "tape_restore.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <complex>
#include <numeric>

namespace trestore {
namespace {

constexpr double kPi = 3.14159265358979323846;

// ------------------------------------------------------------------ the FFT
//
// Iterative radix-2, in place. Only ever called on power-of-two sizes chosen
// here, so it does not carry the machinery to handle anything else.
void fft(std::vector<std::complex<double>>& a, bool inverse) {
    const size_t n = a.size();
    if (n < 2) return;

    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }

    for (size_t len = 2; len <= n; len <<= 1) {
        const double ang = 2 * kPi / (double)len * (inverse ? 1 : -1);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; k++) {
                const std::complex<double> u = a[i + k];
                const std::complex<double> v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse) for (auto& x : a) x /= (double)n;
}

std::vector<float> hann(int n) {
    std::vector<float> w((size_t)n);
    for (int i = 0; i < n; i++)
        w[(size_t)i] = 0.5f * (1.f - std::cos(2.f * (float)kPi * (float)i / (float)(n - 1)));
    return w;
}

// A plain biquad, direct form 1, coefficients from the RBJ cookbook.
struct Biquad {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;

    static Biquad notch(double freq, double sampleRate, double q) {
        Biquad f;
        const double w0 = 2.0 * kPi * freq / sampleRate;
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        f.b0 = 1.0 / a0;
        f.b1 = -2.0 * std::cos(w0) / a0;
        f.b2 = 1.0 / a0;
        f.a1 = -2.0 * std::cos(w0) / a0;
        f.a2 = (1.0 - alpha) / a0;
        return f;
    }

    void runForward(std::vector<float>& x) const {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (float& s : x) {
            const double in = s;
            const double out = b0 * in + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = in; y2 = y1; y1 = out;
            s = (float)out;
        }
    }

    // Forwards then backwards: squares the magnitude response, so the notch
    // gets deeper, and cancels the phase exactly.
    //
    // Both passes start from zero state, which is a step against whatever the
    // signal does at that edge, and a notch rings at the frequency it is
    // removing - so the ring survives as residual hum at the head and tail.
    // The margin either side is an odd extension, reflected through the end
    // sample rather than mirrored about it: a mirror repeats that sample and
    // leaves a corner in the waveform, which measured worse than no padding
    // at all. Reflecting through the point keeps value and slope continuous.
    void runZeroPhase(std::vector<float>& x) const {
        if (x.size() < 4) return;
        const size_t margin = std::min<size_t>(x.size() - 1, 16384);
        const size_t n = x.size();

        std::vector<float> padded;
        padded.reserve(n + 2 * margin);
        for (size_t i = 0; i < margin; i++) padded.push_back(2.f * x[0] - x[margin - i]);
        padded.insert(padded.end(), x.begin(), x.end());
        for (size_t i = 0; i < margin; i++) padded.push_back(2.f * x[n - 1] - x[n - 2 - i]);

        runForward(padded);
        std::reverse(padded.begin(), padded.end());
        runForward(padded);
        std::reverse(padded.begin(), padded.end());

        std::copy(padded.begin() + (long)margin,
                  padded.begin() + (long)(margin + n), x.begin());
    }
};

} // anonymous

double linearToDb(double linear) { return linear > 1e-12 ? 20.0 * std::log10(linear) : -240.0; }
double dbToLinear(double db) { return std::pow(10.0, db / 20.0); }

float peak(const Clip& c) {
    float p = 0.f;
    for (const auto& v : c.ch) for (float s : v) p = std::max(p, std::fabs(s));
    return p;
}

void removeDcOffset(Clip& c) {
    for (auto& v : c.ch) {
        if (v.empty()) continue;
        // Accumulated as a double: millions of samples, and a float running
        // total loses the very offset being measured.
        const double mean = std::accumulate(v.begin(), v.end(), 0.0) / (double)v.size();
        for (float& s : v) s = (float)(s - mean);
    }
}

void removeHum(Clip& c, const HumOptions& o) {
    if (c.empty() || o.harmonics <= 0) return;
    for (int h = 1; h <= o.harmonics; h++) {
        const double f = o.baseHz * h;
        if (f >= c.sampleRate * 0.5) break;              // above Nyquist
        const Biquad bq = Biquad::notch(f, c.sampleRate, o.q);
        for (auto& v : c.ch) bq.runZeroPhase(v);
    }
}

NoiseProfile measureNoise(const Clip& c, double startSec, double lenSec, int fftSize) {
    NoiseProfile p;
    if (c.empty() || fftSize < 32) return p;

    const size_t start = (size_t)std::max(0.0, startSec * c.sampleRate);
    const size_t len   = (size_t)std::max(0.0, lenSec * c.sampleRate);
    if (start >= c.frames()) return p;
    const size_t end = std::min(c.frames(), start + len);
    if (end - start < (size_t)fftSize) return p;

    const int bins = fftSize / 2 + 1;
    const std::vector<float> w = hann(fftSize);
    std::vector<double> acc((size_t)bins, 0.0);
    size_t windows = 0;

    std::vector<std::complex<double>> buf((size_t)fftSize);
    const size_t hop = (size_t)fftSize / 2;
    for (int ci = 0; ci < c.channels(); ci++) {
        for (size_t pos = start; pos + (size_t)fftSize <= end; pos += hop) {
            for (int i = 0; i < fftSize; i++)
                buf[(size_t)i] = c.ch[(size_t)ci][pos + (size_t)i] * w[(size_t)i];
            fft(buf, false);
            for (int b = 0; b < bins; b++) acc[(size_t)b] += std::abs(buf[(size_t)b]);
            windows++;
        }
    }
    if (windows == 0) return p;

    p.mag.resize((size_t)bins);
    for (int b = 0; b < bins; b++) p.mag[(size_t)b] = (float)(acc[(size_t)b] / (double)windows);
    p.fftSize = fftSize;
    p.sampleRate = c.sampleRate;
    return p;
}

NoiseProfile measureQuietest(const Clip& c, double lenSec, int fftSize) {
    if (c.empty()) return {};
    const size_t len = (size_t)(lenSec * c.sampleRate);
    if (len == 0 || c.frames() < len) return measureNoise(c, 0.0, c.seconds(), fftSize);

    // A coarse RMS envelope, then the quietest window of the wanted length.
    // Cheaper and steadier than testing every sample offset, and the exact
    // placement of a noise measurement does not need to be to the sample.
    const size_t step = std::max<size_t>(1, (size_t)(0.05 * c.sampleRate));
    double bestSum = -1.0;
    size_t bestAt = 0;

    for (size_t pos = 0; pos + len <= c.frames(); pos += step) {
        double sum = 0.0;
        size_t n = 0;
        for (size_t i = pos; i < pos + len; i += 16) {          // sampled, not exhaustive
            for (const auto& v : c.ch) { sum += (double)v[i] * v[i]; n++; }
        }
        const double rms = n ? sum / (double)n : 0.0;
        if (bestSum < 0.0 || rms < bestSum) { bestSum = rms; bestAt = pos; }
    }
    return measureNoise(c, (double)bestAt / c.sampleRate, lenSec, fftSize);
}

bool reduceHiss(Clip& c, const NoiseProfile& p, const HissOptions& o,
                const Progress& onProgress) {
    if (c.empty() || !p.valid()) return false;
    if (std::fabs(p.sampleRate - c.sampleRate) > 0.5) return false;   // profile is not for this clip

    const int n = p.fftSize;
    const int bins = n / 2 + 1;
    if ((int)p.mag.size() != bins) return false;

    const std::vector<float> w = hann(n);
    const size_t hop = (size_t)n / 4;                 // 75% overlap, smooth enough to hide the gain changes
    const float floorGain = (float)dbToLinear(o.floorDb);

    // Counted up front so the fraction is over the whole job rather than
    // restarting at each channel.
    size_t total = 0;
    for (int ci = 0; ci < c.channels(); ci++) {
        const size_t frames = c.ch[(size_t)ci].size();
        if (frames >= (size_t)n) total += (frames - (size_t)n) / hop + 1;
    }
    if (total == 0) return true;

    std::atomic<size_t> done {0};
    std::atomic<bool> stop {false};

    // One stretch of windows, laid into an accumulator that begins at `base`.
    //
    // Overlap-add is a sum, and a sum can be built in pieces: two stretches
    // may be transformed independently and added together afterwards. That is
    // the whole reason this can be split across cores. Each worker keeps its
    // own accumulator covering only the span its own windows reach, and where
    // two spans meet they overlap by three quarters of a window - which the
    // addition at the end resolves exactly, not approximately.
    auto layDown = [&](const std::vector<float>& x, size_t w0, size_t w1, size_t base,
                       std::vector<float>& acc, std::vector<float>& wsum) {
        std::vector<std::complex<double>> buf((size_t)n);
        for (size_t wi = w0; wi < w1; wi++) {
            if ((wi & 63) == 0 && stop.load(std::memory_order_relaxed)) return;

            const size_t pos = wi * hop;
            for (int i = 0; i < n; i++) buf[(size_t)i] = x[pos + (size_t)i] * w[(size_t)i];
            fft(buf, false);

            for (int b = 0; b < bins; b++) {
                const double mag = std::abs(buf[(size_t)b]);
                if (mag <= 1e-20) { buf[(size_t)b] = 0.0; continue; }
                // Subtract the floor, then hold whatever is left above a
                // minimum share of the original rather than letting it reach
                // zero - that minimum is what stops musical noise.
                double gain = (mag - (double)o.amount * (double)p.mag[(size_t)b]) / mag;
                if (gain < floorGain) gain = floorGain;
                if (gain > 1.0) gain = 1.0;
                buf[(size_t)b] *= gain;
                if (b > 0 && b < n / 2) buf[(size_t)(n - b)] = std::conj(buf[(size_t)b]);
            }

            fft(buf, true);
            const size_t at = pos - base;
            for (int i = 0; i < n; i++) {
                acc[at + (size_t)i]  += (float)buf[(size_t)i].real() * w[(size_t)i];
                wsum[at + (size_t)i] += w[(size_t)i] * w[(size_t)i];
            }
            done.fetch_add(1, std::memory_order_relaxed);
        }
    };

    int workers = (o.threads > 0) ? o.threads : (int)std::thread::hardware_concurrency();
    workers = std::max(1, std::min(workers, 16));

    for (int ci = 0; ci < c.channels(); ci++) {
        std::vector<float>& x = c.ch[(size_t)ci];
        const size_t frames = x.size();
        if (frames < (size_t)n) continue;

        const size_t windows = (frames - (size_t)n) / hop + 1;
        std::vector<float> acc(frames + (size_t)n, 0.f);
        std::vector<float> wsum(frames + (size_t)n, 0.f);

        // Below a few hundred windows - a second or so of tape - the threads
        // cost more than they save, and the whole thing is over before a
        // progress bar could have drawn.
        const int k = (windows < 256) ? 1 : workers;

        if (k <= 1) {
            layDown(x, 0, windows, 0, acc, wsum);
            if (onProgress && !onProgress((double)done.load() / (double)total))
                stop.store(true, std::memory_order_relaxed);
        } else {
            struct Part { size_t w0, w1, base; std::vector<float> acc, wsum; };
            std::vector<Part> parts((size_t)k);
            for (int j = 0; j < k; j++) {
                Part& q = parts[(size_t)j];
                q.w0   = windows * (size_t)j / (size_t)k;
                q.w1   = windows * (size_t)(j + 1) / (size_t)k;
                q.base = q.w0 * hop;
                const size_t span = (q.w1 > q.w0) ? (q.w1 - 1 - q.w0) * hop + (size_t)n : 0;
                q.acc.assign(span, 0.f);
                q.wsum.assign(span, 0.f);
            }

            // The callback stays on this thread. Workers only ever touch two
            // atomics, so whoever passed a Progress in does not have to make it
            // thread-safe to use it.
            std::atomic<int> finished {0};
            std::vector<std::thread> pool;
            pool.reserve((size_t)k);
            for (int j = 0; j < k; j++)
                pool.emplace_back([&, j] {
                    Part& q = parts[(size_t)j];
                    layDown(x, q.w0, q.w1, q.base, q.acc, q.wsum);
                    finished.fetch_add(1, std::memory_order_release);
                });

            while (finished.load(std::memory_order_acquire) < k) {
                std::this_thread::sleep_for(std::chrono::milliseconds(8));
                if (onProgress && !onProgress((double)done.load() / (double)total))
                    stop.store(true, std::memory_order_relaxed);
            }
            for (std::thread& t : pool) t.join();

            for (Part& q : parts)
                for (size_t i = 0; i < q.acc.size(); i++) {
                    acc[q.base + i]  += q.acc[i];
                    wsum[q.base + i] += q.wsum[i];
                }
        }

        if (stop.load(std::memory_order_relaxed)) return false;

        // Divide out the accumulated window power rather than assuming the
        // overlap adds to a constant - it does not at the very start and end.
        for (size_t i = 0; i < frames; i++)
            if (wsum[i] > 1e-6f) x[i] = acc[i] / wsum[i];
    }
    return true;
}

std::vector<Track> findTracks(const Clip& c, const TrackOptions& o) {
    std::vector<Track> out;
    if (c.empty()) return out;

    const size_t frames = c.frames();
    const size_t win = std::max<size_t>(1, (size_t)(o.windowSec * c.sampleRate));
    const size_t slots = frames / win;
    if (slots == 0) { out.push_back({0.0, c.seconds()}); return out; }

    // Peak per window rather than RMS: a quiet passage with a loud transient
    // in it is still music, and RMS over fifty milliseconds would call the
    // spaces between piano notes a gap.
    std::vector<float> level(slots, 0.f);
    for (size_t k = 0; k < slots; k++) {
        float pk = 0.f;
        const size_t a = k * win, b = a + win;
        for (const std::vector<float>& ch : c.ch)
            for (size_t i = a; i < b && i < ch.size(); i++)
                pk = std::max(pk, std::fabs(ch[i]));
        level[k] = pk;
    }

    const float quiet = (float)dbToLinear(o.gapDb);
    const size_t minGap = std::max<size_t>(1, (size_t)(o.minGapSec / o.windowSec));

    // Walk the windows, collecting stretches of music separated by gaps long
    // enough to count.
    std::vector<Track> found;
    bool inMusic = false;
    size_t musicStart = 0, quietRun = 0;
    for (size_t k = 0; k < slots; k++) {
        const bool loud = level[k] >= quiet;
        if (loud) {
            if (!inMusic) { inMusic = true; musicStart = k; }
            quietRun = 0;
        } else if (inMusic) {
            if (++quietRun >= minGap) {
                const size_t musicEnd = k + 1 - quietRun;
                found.push_back({(double)musicStart * o.windowSec,
                                 (double)musicEnd * o.windowSec});
                inMusic = false;
                quietRun = 0;
            }
        }
    }
    if (inMusic)
        found.push_back({(double)musicStart * o.windowSec, c.seconds()});

    // Drop the false starts - a click at the head of the tape, the thump of
    // the machine stopping - without letting them break a real song in two.
    for (const Track& t : found)
        if (t.seconds() >= o.minTrackSec) out.push_back(t);

    if (out.empty() && !found.empty()) out.push_back({0.0, c.seconds()});
    if (out.empty()) out.push_back({0.0, c.seconds()});
    return out;
}

Clip extract(const Clip& c, const Track& t) {
    Clip out;
    out.sampleRate = c.sampleRate;
    if (c.empty()) return out;

    const size_t frames = c.frames();
    const size_t a = std::min(frames, (size_t)std::max(0.0, t.startSec * c.sampleRate));
    const size_t b = std::min(frames, (size_t)std::max(0.0, t.endSec * c.sampleRate));
    if (b <= a) return out;

    out.ch.resize(c.ch.size());
    for (size_t ci = 0; ci < c.ch.size(); ci++)
        out.ch[ci].assign(c.ch[ci].begin() + (long)a, c.ch[ci].begin() + (long)b);
    return out;
}

void normalize(Clip& c, double targetPeakDb) {
    const float p = peak(c);
    if (p <= 1e-9f) return;
    const double gain = dbToLinear(targetPeakDb) / (double)p;
    for (auto& v : c.ch) for (float& s : v) s = (float)(s * gain);
}

} // namespace trestore
