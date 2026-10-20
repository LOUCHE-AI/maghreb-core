#include "audio_import.h"
#include "wav_io.h"

#if !defined(_WIN32)
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#else
#include <process.h>
#define getpid _getpid
#endif

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>

#if !defined(_WIN32)
extern char** environ;
#endif

namespace maudio {
namespace fs = std::filesystem;
namespace {

constexpr double kPi = 3.14159265358979323846;

std::string lowerExt(const std::string& path) {
    const size_t dot = path.find_last_of('.');
    if (dot == std::string::npos) return {};
    std::string e = path.substr(dot + 1);
    for (char& ch : e) ch = (char)std::tolower((unsigned char)ch);
    return e;
}

std::string onPath(const char* name) {
#if defined(_WIN32)
    (void)name; return {}; // ponytail: no ffmpeg on Windows yet, so WAV only
#else
    const char* p = std::getenv("PATH");
    if (!p) return {};
    const std::string path(p);
    size_t start = 0;
    while (start <= path.size()) {
        const size_t colon = path.find(':', start);
        const std::string item = path.substr(
            start, colon == std::string::npos ? std::string::npos : colon - start);
        if (!item.empty()) {
            std::error_code ec;
            const fs::path cand = fs::path(item) / name;
            if (fs::exists(cand, ec) && ::access(cand.c_str(), X_OK) == 0) return cand.string();
        }
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return {};
#endif
}

void setErr(std::string* err, const std::string& m) { if (err) *err = m; }

// Runs a command to completion, quietly. Used only for ffmpeg.
bool run(const std::vector<std::string>& argv) {
#if defined(_WIN32)
    (void)argv; return false;
#else
    std::vector<char*> c;
    c.reserve(argv.size() + 1);
    for (const auto& a : argv) c.push_back(const_cast<char*>(a.c_str()));
    c.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    pid_t pid = -1;
    const int rc = posix_spawn(&pid, c[0], &fa, nullptr, c.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return false;

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}

} // anonymous

const char* const kExtensions[] = {"wav", "mp3", "flac", "m4a", "aif", "aiff", "ogg", "opus", "wma"};
const int kNumExtensions = (int)(sizeof(kExtensions) / sizeof(kExtensions[0]));

bool haveFfmpeg() { return !onPath("ffmpeg").empty(); }

void resample(std::vector<std::vector<float>>& channels, double from, double to) {
    if (channels.empty() || channels[0].empty()) return;
    if (from <= 0 || to <= 0 || std::fabs(from - to) < 1e-9) return;

    const double ratio = to / from;
    const size_t inN = channels[0].size();
    const size_t outN = (size_t)std::llround((double)inN * ratio);
    if (outN == 0) return;

    // Going down, the filter has to cut at the new Nyquist rather than the old
    // one, or everything above it folds back as aliasing. Going up there is
    // nothing above the old Nyquist to remove, so the cutoff stays there.
    const double cutoff = std::min(1.0, ratio);
    // Sixty-five taps. Measured on an 18 kHz tone taken from 48k to 32k -
    // the hard case, because 18 kHz lands just above the new Nyquist and has
    // to be removed rather than merely attenuated:
    //
    //      33 taps  -28.7 dB      97 taps  -81.3 dB
    //      65 taps  -75.3 dB     129 taps  -90.5 dB
    //
    // The knee is between 33 and 65, and past that it is paying steadily more
    // for attenuation already far below anything audible. Resampling only
    // happens when the rates differ, and only once per import.
    const int half = 32;

    std::vector<std::vector<float>> out(channels.size(), std::vector<float>(outN, 0.f));

    for (size_t ci = 0; ci < channels.size(); ci++) {
        const std::vector<float>& in = channels[ci];
        for (size_t i = 0; i < outN; i++) {
            const double centre = (double)i / ratio;
            const long base = (long)std::floor(centre);

            double acc = 0.0, wsum = 0.0;
            for (int k = -half; k <= half; k++) {
                const long idx = base + k;
                if (idx < 0 || idx >= (long)inN) continue;
                const double x = (centre - (double)idx) * cutoff;

                double sinc = 1.0;
                if (std::fabs(x) > 1e-9) sinc = std::sin(kPi * x) / (kPi * x);

                // Blackman over the whole window, so the taps taper to nothing
                // at the ends instead of stopping abruptly and ringing.
                const double t = (double)(k + half) / (double)(2 * half);
                const double win = 0.42 - 0.5 * std::cos(2 * kPi * t) + 0.08 * std::cos(4 * kPi * t);

                const double w = sinc * win;
                acc += in[(size_t)idx] * w;
                wsum += w;
            }
            out[ci][i] = (float)(wsum > 1e-9 ? acc / wsum : 0.0);
        }
    }
    channels.swap(out);
}

bool importAudio(const std::string& path, std::vector<std::vector<float>>& out,
                 double targetRate, std::string* err) {
    out.clear();
    std::error_code ec;
    if (!fs::exists(path, ec)) { setErr(err, "no such file"); return false; }

    const std::string ext = lowerExt(path);
    int rate = 0;

    if (ext == "wav" && mwav::readWav(path, out, rate) && !out.empty()) {
        if (targetRate > 0 && rate > 0) resample(out, rate, targetRate);
        return true;
    }

    // Not a WAV, or a WAV this cannot read - hand it to ffmpeg. Decoded to
    // 32-bit float at the target rate, so no conversion is left to do here.
    const std::string ffmpeg = onPath("ffmpeg");
    if (ffmpeg.empty()) {
        setErr(err, ext == "wav" ? "this WAV is in an encoding I cannot read, and ffmpeg "
                                   "is not installed to convert it"
                                 : "only WAV can be read directly - install ffmpeg for "
                                   "mp3 and the rest");
        return false;
    }

    const fs::path tmp = fs::temp_directory_path() /
                         ("taperepair-import-" + std::to_string(::getpid()) + ".wav");
    fs::remove(tmp, ec);

    const std::string rateArg = std::to_string((int)std::llround(targetRate > 0 ? targetRate : 48000));
    const bool okRun = run({ffmpeg, "-v", "error", "-nostdin", "-y",
                            "-i", path,
                            "-map", "a:0",          // the first audio stream, ignore any video
                            "-c:a", "pcm_f32le",
                            "-ar", rateArg,
                            tmp.string()});
    if (!okRun) {
        fs::remove(tmp, ec);
        setErr(err, "ffmpeg could not read that file");
        return false;
    }

    const bool okRead = mwav::readWav(tmp.string(), out, rate) && !out.empty();
    fs::remove(tmp, ec);
    if (!okRead) { setErr(err, "could not read what ffmpeg produced"); return false; }
    return true;
}

} // namespace maudio
