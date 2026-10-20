#include "tapegrain_separate.h"
#include "tapegrain_dsp.h"
#include "tapegrain_stems.h"

#include <filesystem>
#include <cstdlib>
#include <cstring>

#if defined(_WIN32)
// Not wired up on Windows yet. Everything below is POSIX process handling and
// has no honest Windows equivalent short of CreateProcess and overlapped
// pipes; saying so is better than pretending the button works.
#else
#include <spawn.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/wait.h>
#include <signal.h>
extern char** environ;
#endif

namespace tg {
namespace {

namespace fs = std::filesystem;

bool runs(const std::vector<std::string>& argv);

// Windows has no execute bit to ask about; a regular file is as close as it gets.
bool isExecutable(const fs::path& p) {
    std::error_code ec;
#if defined(_WIN32)
    return fs::is_regular_file(p, ec);
#else
    return fs::exists(p, ec) && ::access(p.c_str(), X_OK) == 0;
#endif
}

// Is there an executable of this name on PATH?
std::string onPath(const char* name) {
    const char* p = std::getenv("PATH");
    if (!p) return {};
#if defined(_WIN32)
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::string path(p), item;
    size_t start = 0;
    while (start <= path.size()) {
        const size_t colon = path.find(sep, start);
        item = path.substr(start, colon == std::string::npos ? std::string::npos
                                                             : colon - start);
        if (!item.empty()) {
            const fs::path cand = fs::path(item) / name;
            if (isExecutable(cand)) return cand.string();
#if defined(_WIN32)
            fs::path exe = cand;
            exe += ".exe";
            if (isExecutable(exe)) return exe.string();
#endif
        }
        if (colon == std::string::npos) break;
        start = colon + 1;
    }
    return {};
}

// The environment the child gets: this process's, plus one thing.
//
// Demucs runs its convolutions through MIOpen, and on this machine's AMD part
// every one of them aborts with miopenStatusUnknownError unless the find mode
// is FAST - measured: the same three second file fails outright without it and
// separates in under four seconds with it. A host is not going to be launched
// with that set, and a machine without MIOpen ignores it, so the child is
// handed it here rather than the user being asked to arrange it.
#if !defined(_WIN32)
std::vector<std::string> childEnvironment() {
    std::vector<std::string> env;
    bool hasFindMode = false;
    for (char** e = environ; e && *e; ++e) {
        env.emplace_back(*e);
        if (std::strncmp(*e, "MIOPEN_FIND_MODE=", 17) == 0) hasFindMode = true;
    }
    if (!hasFindMode) env.emplace_back("MIOPEN_FIND_MODE=FAST");
    return env;
}
#endif

// The last non-empty line of whatever the child printed - Demucs draws a
// progress bar, and its last line is the most informative thing available
// without parsing its output format.
std::string lastLine(const std::string& s) {
    size_t end = s.find_last_not_of("\r\n \t");
    if (end == std::string::npos) return {};
    size_t start = s.find_last_of("\r\n", end);
    return s.substr(start == std::string::npos ? 0 : start + 1,
                    end - (start == std::string::npos ? 0 : start));
}

// Demucs writes <outRoot>/<model>/<track>/vocals.wav. The model folder is
// named after whichever model ran, so rather than hard-coding "htdemucs" this
// looks for the first folder underneath that holds a part we recognise.
std::string findProducedPart(const std::string& outRoot) {
    std::error_code ec;
    if (!fs::exists(outRoot, ec)) return {};
    std::string out[kStemSlots];
    for (fs::recursive_directory_iterator it(outRoot,
             fs::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec)) {
        if (ec) { ec.clear(); continue; }
        if (!it->is_regular_file(ec)) continue;
        if (findStemSet(it->path().string(), out) == kStemSlots)
            return it->path().string();
    }
    return {};
}

} // anonymous

std::vector<std::string> demucsCommand() {
    if (const char* env = std::getenv("TAPEGRAIN_DEMUCS")) {
        if (*env) return {env};
    }
    if (std::string p = onPath("demucs"); !p.empty()) return {p};

    // The virtualenv this plug-in's own instructions tell people to make. A
    // venv is not on PATH and its packages are not importable from the system
    // interpreter, so without looking here the usual install is invisible -
    // which is exactly what happened the first time.
    {
        fs::path base;
        if (const char* x = std::getenv("XDG_DATA_HOME"); x && *x) base = x;
        else if (const char* h = std::getenv("HOME"); h && *h) base = fs::path(h) / ".local" / "share";
        if (!base.empty()) {
            const fs::path cand = base / "TapeGrain" / "venv" / "bin" / "demucs";
            if (isExecutable(cand)) return {cand.string()};
        }
    }

    // A pip install into a user site-packages often has no console script.
    if (std::string py = onPath("python3"); !py.empty())
        if (runs({py, "-c", "import demucs"})) return {py, "-m", "demucs"};
    return {};
}

namespace {
// Runs a command to completion purely for its exit status. Used to probe for
// the demucs module; nothing long-running goes through here.
bool runs(const std::vector<std::string>& argv) {
#if defined(_WIN32)
    (void)argv; return false;
#else
    std::vector<char*> cargv;
    for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_addopen(&fa, STDOUT_FILENO, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&fa, STDERR_FILENO, "/dev/null", O_WRONLY, 0);

    pid_t pid = -1;
    const int rc = posix_spawn(&pid, cargv[0], &fa, nullptr, cargv.data(), environ);
    posix_spawn_file_actions_destroy(&fa);
    if (rc != 0) return false;

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) return false;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
#endif
}
} // anonymous

Separator::~Separator() { cancel(); }

bool Separator::start(const std::string& input, const std::string& outRoot) {
#if defined(_WIN32)
    (void)input; (void)outRoot;
    state_ = State::Failed;
    message_ = "Separation is not wired up on Windows yet.";
    return false;
#else
    if (state_ == State::Running) { message_ = "Already separating."; return false; }

    resultPart_.clear();
    partial_.clear();
    outRoot_ = outRoot;

    std::error_code ec;
    if (!fs::exists(input, ec)) {
        finish(false, "That file is gone.");
        return false;
    }

    auto argv = demucsCommand();
    if (argv.empty()) {
        finish(false, "Demucs not found. Put it on PATH, install it into "
                      "~/.local/share/TapeGrain/venv, or point TAPEGRAIN_DEMUCS at it.");
        return false;
    }
    argv.insert(argv.end(), {"-o", outRoot, input});

    fs::create_directories(outRoot, ec);

    int fds[2];
    if (::pipe(fds) != 0) { finish(false, "Could not open a pipe."); return false; }

    std::vector<char*> cargv;
    for (const auto& a : argv) cargv.push_back(const_cast<char*>(a.c_str()));
    cargv.push_back(nullptr);

    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    // Both streams to the same pipe: Demucs reports on stderr, but it is not
    // worth two descriptors to keep them apart when the whole use of the
    // output is one status line.
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fa, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fa, fds[0]);

    std::vector<std::string> env = childEnvironment();
    std::vector<char*> cenv;
    for (auto& e : env) cenv.push_back(const_cast<char*>(e.c_str()));
    cenv.push_back(nullptr);

    pid_t pid = -1;
    const int rc = posix_spawn(&pid, cargv[0], &fa, nullptr, cargv.data(), cenv.data());
    posix_spawn_file_actions_destroy(&fa);
    ::close(fds[1]);

    if (rc != 0) {
        ::close(fds[0]);
        finish(false, std::string("Could not start Demucs: ") + std::strerror(rc));
        return false;
    }

    // Non-blocking, because poll() runs on the timer that also redraws.
    ::fcntl(fds[0], F_SETFL, ::fcntl(fds[0], F_GETFL, 0) | O_NONBLOCK);
    pid_ = pid;
    fd_  = fds[0];
    state_ = State::Running;
    message_ = "Separating...";
    return true;
#endif
}

void Separator::poll() {
#if !defined(_WIN32)
    if (state_ != State::Running) return;

    if (fd_ >= 0) {
        char buf[4096];
        for (;;) {
            const ssize_t n = ::read(fd_, buf, sizeof buf);
            if (n <= 0) break;
            partial_.append(buf, (size_t)n);
            if (partial_.size() > 8192) partial_.erase(0, partial_.size() - 8192);
        }
        if (std::string line = lastLine(partial_); !line.empty())
            message_ = line;
    }

    int status = 0;
    const pid_t r = ::waitpid(pid_, &status, WNOHANG);
    if (r == 0) return;                       // still going

    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    pid_ = -1;

    const bool ok = (r > 0) && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!ok) { finish(false, "Demucs failed. " + message_); return; }

    resultPart_ = findProducedPart(outRoot_);
    if (resultPart_.empty()) finish(false, "Demucs finished but produced nothing readable.");
    else                     finish(true, "Separated.");
#endif
}

void Separator::cancel() {
#if !defined(_WIN32)
    if (state_ != State::Running) return;
    if (pid_ > 0) { ::kill(pid_, SIGTERM); ::waitpid(pid_, nullptr, 0); pid_ = -1; }
    if (fd_ >= 0) { ::close(fd_); fd_ = -1; }
    finish(false, "Cancelled.");
#endif
}

void Separator::finish(bool ok, const std::string& why) {
    state_ = ok ? State::Done : State::Failed;
    message_ = why;
}

} // namespace tg
