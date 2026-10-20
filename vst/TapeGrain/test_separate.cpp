#include "tapegrain_separate.h"
#include <cstdio>
#include <thread>
#include <chrono>
#include <filesystem>
#include "tapegrain_dsp.h"
#include "tapegrain_stems.h"
static bool findsAll(const std::string& p){ std::string o[tg::kStemSlots]; return tg::findStemSet(p,o)==tg::kStemSlots; }
int fails = 0;
void ok(const char* l, bool c, const std::string& x=""){ printf("%s  %s%s%s\n", c?"PASS ":"FAIL ", l, x.empty()?"":"  ", x.c_str()); if(!c) fails++; }
int main(){
    namespace fs = std::filesystem;
    const std::string realPath = std::getenv("PATH") ? std::getenv("PATH") : "";
    auto root = fs::temp_directory_path()/"tg_sep_test";
    fs::remove_all(root); fs::create_directories(root);
    auto in = root/"mix.wav"; { FILE* h=fopen(in.c_str(),"wb"); if(h) fclose(h); }

    { // demucs absent. XDG_DATA_HOME is pointed at an empty sandbox too,
      // or this picks up a real venv on the machine running the test.
        unsetenv("TAPEGRAIN_DEMUCS");
        setenv("PATH", "/nonexistent", 1);
        setenv("XDG_DATA_HOME", (root/"empty-xdg").c_str(), 1);
        tg::Separator s;
        bool started = s.start(in.string(), (root/"out1").string());
        ok("missing demucs fails cleanly, does not start",
           !started && s.state()==tg::Separator::State::Failed, s.message());
        unsetenv("XDG_DATA_HOME");
    }
    setenv("PATH", realPath.c_str(), 1);   // the stand-in needs mkdir/basename
    { // the venv the instructions tell people to make must be found
        unsetenv("TAPEGRAIN_DEMUCS");
        setenv("PATH", "/nonexistent", 1);
        auto venv = root/"xdg"/"TapeGrain"/"venv"/"bin";
        fs::create_directories(venv);
        { FILE* h = fopen((venv/"demucs").c_str(), "wb"); if (h) fclose(h); }
        fs::permissions(venv/"demucs", fs::perms::owner_all, fs::perm_options::add);
        setenv("XDG_DATA_HOME", (root/"xdg").c_str(), 1);
        auto cmd = tg::demucsCommand();
        ok("demucs is found in TapeGrain's own venv, not just on PATH",
           cmd.size() == 1 && cmd[0] == (venv/"demucs").string(),
           cmd.empty() ? "not found" : cmd[0]);
        unsetenv("XDG_DATA_HOME");
    }
    setenv("PATH", realPath.c_str(), 1);
    { // full run against the stand-in
        setenv("TAPEGRAIN_DEMUCS", std::getenv("FAKE_DEMUCS"), 1);
        tg::Separator s;
        bool started = s.start(in.string(), (root/"out2").string());
        ok("start() launches", started && s.state()==tg::Separator::State::Running, s.message());
        for (int i=0;i<200 && s.state()==tg::Separator::State::Running;i++){
            s.poll(); std::this_thread::sleep_for(std::chrono::milliseconds(30));
        }
        ok("run completes", s.state()==tg::Separator::State::Done, s.message());
        ok("a produced part is found", !s.resultPart().empty(), s.resultPart());
        bool isPart = false;
        for (const char* n : {"vocals.wav","drums.wav","bass.wav","other.wav"})
            if (s.resultPart().find(n)!=std::string::npos) isPart = true;
        ok("the part is one of the four (any resolves the set)", isPart, s.resultPart());
        std::string set[4];
        ok("and it resolves to all four slots",
           tg::kStemSlots==4 && findsAll(s.resultPart()), s.resultPart());
    }
    { // cancel
        setenv("TAPEGRAIN_DEMUCS", std::getenv("FAKE_DEMUCS"), 1);
        tg::Separator s;
        s.start(in.string(), (root/"out3").string());
        s.cancel();
        ok("cancel stops the run", s.state()==tg::Separator::State::Failed, s.message());
    }
    fs::remove_all(root);
    printf("\n%s\n", fails?"FAILURES":"all good");
    return fails?1:0;
}
