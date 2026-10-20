// Buttons a hardware controller can press: Next Patch, Repair, Render Loop.
//
// A host hands a plug-in its parameter changes on the audio thread, and
// nothing these buttons do belongs there - a cartridge is read from disk, a
// take is denoised, a MIDI file is written. So process() only notices a
// trigger parameter rising past halfway and posts it, and the work happens on
// a thread that is allowed to block. While the editor is open that is the
// editor's own timer, so a press from the controller shows in the window the
// way a click would. With it closed, a thread of the plug-in's own takes it.
//
// Rising edges only. A pad sends on when pressed and off when let go, and that
// is one press, so pads want mapping as momentary. A toggle fires on every
// other press.
//
// These parameters are never saved: a Set written while a pad was held would
// otherwise press the button again every time it was opened.

#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

namespace mplat {

class Triggers {
public:
    static const int kMax = 16;
    using Run = std::function<void(int trigger)>;

    // Audio thread. v is the parameter's normalised value.
    void feed(int t, double v) {
        if (t < 0 || t >= kMax) return;
        const bool high = v >= 0.5;
        if (high && !high_[t]) pending_.fetch_or(1u << t, std::memory_order_acq_rel);
        high_[t] = high;
    }

    // Any thread but the audio one: runs whatever was pressed since last time.
    void drain(const Run& run) {
        const unsigned bits = pending_.exchange(0, std::memory_order_acq_rel);
        for (int t = 0; t < kMax; t++)
            if (bits & (1u << t)) run(t);
    }

    // The thread that presses the buttons while no editor is open. Fifty
    // looks a second is quicker than a hand notices and costs nothing.
    void start(Run run) {
        stop();
        run_ = std::move(run);
        quit_.store(false);
        thread_ = std::thread([this] {
            while (!quit_.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
                if (!editorOpen_.load()) drain(run_);
            }
        });
    }

    void stop() {
        quit_.store(true);
        if (thread_.joinable()) thread_.join();
    }

    ~Triggers() { stop(); }

    // ponytail: the hand-over is a flag, not a lock. A press landing in the
    // instant the editor opens can run on the old thread while the window
    // draws; add a mutex around the actions if that ever shows.
    void setEditorOpen(bool open) { editorOpen_.store(open); }

private:
    bool high_[kMax] = {};
    std::atomic<unsigned> pending_ {0};
    std::atomic<bool> editorOpen_ {false};
    std::atomic<bool> quit_ {false};
    std::thread thread_;
    Run run_;
};

} // namespace mplat
