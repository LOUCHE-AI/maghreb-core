// Running a separator, which is not something this plug-in can do itself.
//
// Splitting a mix into four parts is a neural network, not a filter: the
// quality difference between a trained model and anything achievable with an
// STFT and some median filtering is not close. So TapeGrain does not try. It
// drives Demucs, which is the model most people already have, and loads what
// comes out through the same slots that already read Live's stem folders.
//
// The run is a child process and it takes minutes, not milliseconds, so
// nothing here blocks. start() launches and returns; poll() is called from the
// editor's idle timer and moves the state along. The audio thread never sees
// any of this.

#pragma once

#include <string>
#include <vector>

namespace tg {

// The command that runs Demucs on this machine, as argv, or empty if it is not
// installed. Checked in order: TAPEGRAIN_DEMUCS (a full path), "demucs" on
// PATH, then "python3 -m demucs" - which is how a pip install without a
// console script ends up being reachable.
std::vector<std::string> demucsCommand();

class Separator {
public:
    enum class State { Idle, Running, Done, Failed };

    ~Separator();
    Separator() = default;
    Separator(const Separator&) = delete;
    Separator& operator=(const Separator&) = delete;

    // Starts a run. `outRoot` is a folder this owns; Demucs creates
    // <outRoot>/<model>/<track>/ underneath it. False if a run is already
    // going or Demucs cannot be found - message() says which.
    bool start(const std::string& input, const std::string& outRoot);

    // Reaps the child and reads whatever it has printed. Cheap; call it from
    // an idle timer.
    void poll();

    void cancel();

    State state() const { return state_; }

    // For the editor: a line to show the user, whatever the state.
    const std::string& message() const { return message_; }

    // On Done, one of the four produced files - hand it to loadStemSet and the
    // other three come with it. Empty otherwise.
    const std::string& resultPart() const { return resultPart_; }

private:
    void finish(bool ok, const std::string& why);

    State state_ = State::Idle;
    std::string message_, resultPart_, outRoot_, partial_;
    int pid_ = -1;
    int fd_  = -1;
};

} // namespace tg
