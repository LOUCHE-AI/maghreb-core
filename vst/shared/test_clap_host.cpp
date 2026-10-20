// A CLAP host, as small as one can be and still prove something.
//
// Bitwig is the only CLAP host on this machine, and it is not a thing a test
// can drive. So this does what a host does: loads the module, makes the
// plug-in, runs audio through it, then opens its window in a real X11 frame and
// drives it with the timers and file descriptors the plug-in asks for. That
// last part is the whole reason it exists - the run loop is the piece being
// tested, and it cannot be checked without something to run it.
//
//   test_clap_host <file.clap> [seconds]

#include <clap/clap.h>
#include <X11/Xlib.h>
#include <dlfcn.h>
#include <sys/select.h>
#include <dirent.h>
#include <ctime>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

static int fails = 0;
static void ok(const char* l, bool c, const std::string& d = "") {
    printf("%s  %s%s%s\n", c ? "PASS " : "FAIL ", l, d.empty() ? "" : "  ", d.c_str());
    if (!c) fails++;
}

namespace {

struct Timer { uint32_t periodMs; double nextAt; };

struct Host {
    clap_host_t host{};
    const clap_plugin_t* plug = nullptr;
    std::map<clap_id, Timer> timers;
    std::vector<int> fds;
    clap_id nextTimer = 1;
    uint32_t resizeW = 0, resizeH = 0;
};

Host g;

double nowSeconds() {
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

bool CLAP_ABI regTimer(const clap_host_t*, uint32_t periodMs, clap_id* id) {
    *id = g.nextTimer++;
    g.timers[*id] = {periodMs ? periodMs : 16,
                     nowSeconds() + (periodMs ? periodMs : 16) / 1000.0};
    return true;
}
bool CLAP_ABI unregTimer(const clap_host_t*, clap_id id) { return g.timers.erase(id) > 0; }
const clap_host_timer_support_t kTimers = {regTimer, unregTimer};

bool CLAP_ABI regFd(const clap_host_t*, int fd, clap_posix_fd_flags_t) {
    g.fds.push_back(fd);
    return true;
}
bool CLAP_ABI modFd(const clap_host_t*, int, clap_posix_fd_flags_t) { return true; }
bool CLAP_ABI unregFd(const clap_host_t*, int fd) {
    for (size_t i = 0; i < g.fds.size(); i++)
        if (g.fds[i] == fd) { g.fds.erase(g.fds.begin() + i); return true; }
    return false;
}
const clap_host_posix_fd_support_t kFds = {regFd, modFd, unregFd};

void CLAP_ABI hintsChanged(const clap_host_t*) {}
bool CLAP_ABI requestResize(const clap_host_t*, uint32_t w, uint32_t h) {
    g.resizeW = w; g.resizeH = h;
    return true;
}
bool CLAP_ABI requestShowHide(const clap_host_t*) { return true; }
void CLAP_ABI guiClosed(const clap_host_t*, bool) {}
const clap_host_gui_t kGui = {hintsChanged, requestResize, requestShowHide,
                              requestShowHide, guiClosed};

const void* CLAP_ABI getExt(const clap_host_t*, const char* id) {
    if (!std::strcmp(id, CLAP_EXT_TIMER_SUPPORT))    return &kTimers;
    if (!std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT)) return &kFds;
    if (!std::strcmp(id, CLAP_EXT_GUI))              return &kGui;
    return nullptr;
}

// Everything the plug-in registered, serviced the way a host services it.
void pump(double seconds) {
    const clap_plugin_timer_support_t* pt = (const clap_plugin_timer_support_t*)
        g.plug->get_extension(g.plug, CLAP_EXT_TIMER_SUPPORT);
    const clap_plugin_posix_fd_support_t* pf = (const clap_plugin_posix_fd_support_t*)
        g.plug->get_extension(g.plug, CLAP_EXT_POSIX_FD_SUPPORT);

    const double until = nowSeconds() + seconds;
    while (nowSeconds() < until) {
        fd_set set;
        FD_ZERO(&set);
        int maxFd = -1;
        for (int fd : g.fds) { FD_SET(fd, &set); if (fd > maxFd) maxFd = fd; }

        timeval tv{0, 8000};
        if (maxFd >= 0) select(maxFd + 1, &set, nullptr, nullptr, &tv);
        else select(0, nullptr, nullptr, nullptr, &tv);

        if (pf) {
            std::vector<int> ready;
            for (int fd : g.fds) if (FD_ISSET(fd, &set)) ready.push_back(fd);
            for (int fd : ready) pf->on_fd(g.plug, fd, CLAP_POSIX_FD_READ);
        }
        if (pt) {
            const double t = nowSeconds();
            for (auto& kv : g.timers) {
                if (t < kv.second.nextAt) continue;
                kv.second.nextAt = t + kv.second.periodMs / 1000.0;
                pt->on_timer(g.plug, kv.first);
            }
        }
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) { printf("usage: test_clap_host <file.clap> [seconds]\n"); return 2; }
    const char* path = argv[1];
    const double seconds = argc > 2 ? atof(argv[2]) : 2.0;
    char msg[400];

    void* lib = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    ok("the module opens", lib != nullptr, lib ? path : dlerror());
    if (!lib) return 1;

    const clap_plugin_entry_t* entry =
        (const clap_plugin_entry_t*)dlsym(lib, "clap_entry");
    ok("and exports clap_entry", entry && entry->init(path));
    if (!entry) return 1;

    const clap_plugin_factory_t* factory =
        (const clap_plugin_factory_t*)entry->get_factory(CLAP_PLUGIN_FACTORY_ID);
    ok("with a factory holding one plug-in",
       factory && factory->get_plugin_count(factory) == 1);
    if (!factory) return 1;

    const clap_plugin_descriptor_t* desc = factory->get_plugin_descriptor(factory, 0);
    snprintf(msg, sizeof msg, "%s", desc ? desc->name : "?");
    ok("which names itself", desc && desc->name && *desc->name, msg);

    g.host.clap_version = CLAP_VERSION_INIT;
    g.host.host_data = nullptr;
    g.host.name = "clap smoke host";
    g.host.vendor = "test";
    g.host.url = "";
    g.host.version = "1";
    g.host.get_extension = getExt;
    g.host.request_restart = [](const clap_host_t*) {};
    g.host.request_process = [](const clap_host_t*) {};
    g.host.request_callback = [](const clap_host_t*) {};

    const clap_plugin_t* p = factory->create_plugin(factory, &g.host, desc->id);
    ok("and makes one that initialises", p && p->init(p));
    if (!p) return 1;
    g.plug = p;

    const clap_plugin_params_t* pars =
        (const clap_plugin_params_t*)p->get_extension(p, CLAP_EXT_PARAMS);
    const uint32_t nParams = pars ? pars->count(p) : 0;
    snprintf(msg, sizeof msg, "%u parameters", nParams);
    ok("it exposes its parameters", nParams > 0, msg);

    bool named = true;
    std::string first;
    for (uint32_t i = 0; i < nParams && named; i++) {
        clap_param_info_t info{};
        if (!pars->get_info(p, i, &info) || !info.name[0]) named = false;
        else if (first.empty()) first = info.name;
    }
    ok("each with a name and a range", named, first.empty() ? "" : "first: " + first);

    ok("it activates", p->activate(p, 48000, 32, 512) && p->start_processing(p));

    // ---- audio: silence in, something finite out ----
    const int block = 256;
    std::vector<float> l(block, 0.f), r(block, 0.f), il(block, 0.f), ir(block, 0.f);
    float* outs[2] = {l.data(), r.data()};
    float* ins[2] = {il.data(), ir.data()};
    clap_audio_buffer_t ab_out{}, ab_in{};
    ab_out.data32 = outs; ab_out.channel_count = 2;
    ab_in.data32 = ins;   ab_in.channel_count = 2;

    const clap_plugin_audio_ports_t* ports = (const clap_plugin_audio_ports_t*)
        p->get_extension(p, CLAP_EXT_AUDIO_PORTS);
    const bool hasIn = ports && ports->count(p, true) > 0;

    struct EmptyEvents {
        static uint32_t CLAP_ABI size(const clap_input_events_t*) { return 0; }
        static const clap_event_header_t* CLAP_ABI get(const clap_input_events_t*, uint32_t) {
            return nullptr;
        }
        static bool CLAP_ABI push(const clap_output_events_t*, const clap_event_header_t*) {
            return true;
        }
    };
    clap_input_events_t inEv{nullptr, EmptyEvents::size, EmptyEvents::get};
    clap_output_events_t outEv{nullptr, EmptyEvents::push};

    bool finite = true;
    for (int b = 0; b < 32; b++) {
        clap_process_t pr{};
        pr.frames_count = block;
        pr.audio_outputs = &ab_out; pr.audio_outputs_count = 1;
        if (hasIn) { pr.audio_inputs = &ab_in; pr.audio_inputs_count = 1; }
        pr.in_events = &inEv;
        pr.out_events = &outEv;
        p->process(p, &pr);
        for (int i = 0; i < block; i++)
            if (!std::isfinite(l[i]) || !std::isfinite(r[i])) finite = false;
    }
    snprintf(msg, sizeof msg, "%d blocks, audio in: %s", 32, hasIn ? "yes" : "no");
    ok("and renders finite audio", finite, msg);

    // ---- knob pages: what a controller's eight encoders turn ----
    if (const clap_plugin_remote_controls_t* rc = (const clap_plugin_remote_controls_t*)
            p->get_extension(p, CLAP_EXT_REMOTE_CONTROLS)) {
        const uint32_t pages = rc->count(p);
        bool real = pages > 0;
        for (uint32_t k = 0; k < pages && real; k++) {
            clap_remote_controls_page_t pg{};
            if (!rc->get(p, k, &pg) || !pg.page_name[0]) { real = false; break; }
            for (int c = 0; c < CLAP_REMOTE_CONTROLS_COUNT && real; c++) {
                double v = 0;
                if (pg.param_ids[c] != CLAP_INVALID_ID && !pars->get_value(p, pg.param_ids[c], &v))
                    real = false;
            }
        }
        snprintf(msg, sizeof msg, "%u pages", pages);
        ok("its knob pages name only real parameters", real, msg);

        // Ableton has no pages: it hands a controller the visible parameters
        // eight at a time, in registered order. So that order has to be the
        // pages exactly, with only the last one short, and there have to be 64
        // or fewer, or Live shows none of them until they are configured.
        std::vector<clap_id> flat;
        bool gapOnlyAtEnd = true;
        for (uint32_t k = 0; k < pages; k++) {
            clap_remote_controls_page_t pg{};
            rc->get(p, k, &pg);
            for (int c = 0; c < CLAP_REMOTE_CONTROLS_COUNT; c++) {
                if (pg.param_ids[c] != CLAP_INVALID_ID) flat.push_back(pg.param_ids[c]);
                else if (k + 1 < pages) gapOnlyAtEnd = false;
            }
        }
        std::vector<clap_id> visible;
        for (uint32_t i = 0; i < nParams; i++) {
            clap_param_info_t info{};
            if (pars->get_info(p, i, &info) && !(info.flags & CLAP_PARAM_IS_HIDDEN))
                visible.push_back(info.id);
        }
        snprintf(msg, sizeof msg, "%zu visible, %zu on the pages", visible.size(), flat.size());
        ok("and the registered order is those pages, eight at a time",
           gapOnlyAtEnd && visible == flat, msg);
        ok("with 64 or fewer showing, so Live shows them unconfigured", visible.size() <= 64, msg);
    }

    // ---- buttons: a controller pressing them ----
    //
    // Only with CLAP_TEST_PRESS set, because pressing them does things: run it
    // with XDG_DATA_HOME pointed at a scratch folder holding a cartridge or two.
    const bool pressing = std::getenv("CLAP_TEST_PRESS") != nullptr;
    auto idOf = [&](const char* name) -> clap_id {
        for (uint32_t i = 0; i < nParams; i++) {
            clap_param_info_t info{};
            if (pars->get_info(p, i, &info) && !std::strcmp(info.name, name)) return info.id;
        }
        return CLAP_INVALID_ID;
    };
    // One block with the value, then one with it let go: a pad press.
    auto press = [&](clap_id id) {
        for (double v : {1.0, 0.0}) {
            clap_event_param_value_t e{};
            e.header.size = sizeof e;
            e.header.type = CLAP_EVENT_PARAM_VALUE;
            e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            e.param_id = id;
            e.value = v;
            e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
            struct One {
                static uint32_t CLAP_ABI size(const clap_input_events_t*) { return 1; }
                static const clap_event_header_t* CLAP_ABI get(
                    const clap_input_events_t* l, uint32_t) {
                    return (const clap_event_header_t*)l->ctx;
                }
            };
            clap_input_events_t one{&e, One::size, One::get};
            clap_process_t pr{};
            pr.frames_count = block;
            pr.audio_outputs = &ab_out; pr.audio_outputs_count = 1;
            if (hasIn) { pr.audio_inputs = &ab_in; pr.audio_inputs_count = 1; }
            pr.in_events = &one;
            pr.out_events = &outEv;
            p->process(p, &pr);
        }
        pump(0.25);                           // the trigger thread looks every 20 ms
    };
    const clap_id next = idOf("Next Patch"), voice = idOf("Voice");
    auto voiceNow = [&] { double v = 0; pars->get_value(p, voice, &v); return (int)std::lround(v * 31); };

    if (pressing) {
        if (next != CLAP_INVALID_ID && voice != CLAP_INVALID_ID) {
            press(next);
            double a = 0, b = 0;
            pars->get_value(p, voice, &a);
            press(next);
            pars->get_value(p, voice, &b);
            const int va = (int)std::lround(a * 31), vb = (int)std::lround(b * 31);
            snprintf(msg, sizeof msg, "voice %d, then %d", va + 1, vb + 1);
            ok("Next Patch steps one voice per press", vb == va + 1, msg);
        }

        if (next != CLAP_INVALID_ID && voice == CLAP_INVALID_ID) {
            press(next);
            const clap_plugin_state_t* st =
                (const clap_plugin_state_t*)p->get_extension(p, CLAP_EXT_STATE);
            std::string bytes;
            clap_ostream_t os{&bytes, [](const clap_ostream_t* s, const void* d, uint64_t n) -> int64_t {
                ((std::string*)s->ctx)->append((const char*)d, (size_t)n);
                return (int64_t)n;
            }};
            ok("Next Patch loads a cartridge voice", st && st->save(p, &os) &&
               bytes.find(".syx") != std::string::npos);
        }

        const clap_id midi = idOf("Write MIDI");
        if (midi != CLAP_INVALID_ID) {
            const char* x = std::getenv("XDG_DATA_HOME");
            const std::string dir = std::string(x ? x : "") + "/MaghrebSeq/Exports";
            auto count = [&] {
                int n = 0;
                if (DIR* d = opendir(dir.c_str())) {
                    while (dirent* f = readdir(d))
                        if (std::strstr(f->d_name, ".mid")) n++;
                    closedir(d);
                }
                return n;
            };
            const int before = count();
            press(midi);
            snprintf(msg, sizeof msg, "%d MIDI files, then %d", before, count());
            ok("Write MIDI writes one", count() == before + 1, msg);
        }
    }

    // ---- state: what a project file keeps ----
    //
    // Written blind against the CLAP stream API, and a mistake here loses a
    // preset rather than making a noise, so it is checked by round trip: save,
    // move something, load, and see it come back.
    {
        struct Buf {
            std::vector<char> bytes;
            size_t pos = 0;
            static int64_t CLAP_ABI wr(const clap_ostream_t* s, const void* d, uint64_t n) {
                Buf* b = (Buf*)s->ctx;
                const char* p = (const char*)d;
                b->bytes.insert(b->bytes.end(), p, p + n);
                return (int64_t)n;
            }
            static int64_t CLAP_ABI rd(const clap_istream_t* s, void* d, uint64_t n) {
                Buf* b = (Buf*)s->ctx;
                const size_t left = b->bytes.size() - b->pos;
                const size_t take = n < left ? (size_t)n : left;
                std::memcpy(d, b->bytes.data() + b->pos, take);
                b->pos += take;
                return (int64_t)take;
            }
        };

        const clap_plugin_state_t* st =
            (const clap_plugin_state_t*)p->get_extension(p, CLAP_EXT_STATE);
        ok("it saves and loads state", st != nullptr);

        if (st && nParams > 0) {
            // something continuous, so moving it is not a no-op
            uint32_t which = 0;
            double before = 0;
            bool found = false;
            for (uint32_t i = 0; i < nParams && !found; i++) {
                clap_param_info_t info{};
                if (!pars->get_info(p, i, &info)) continue;
                if (info.flags & CLAP_PARAM_IS_STEPPED) continue;
                which = info.id;
                pars->get_value(p, which, &before);
                found = true;
            }

            Buf buf;
            clap_ostream_t os{&buf, Buf::wr};
            const bool saved = st->save(p, &os);
            snprintf(msg, sizeof msg, "%zu bytes", buf.bytes.size());
            ok("saving writes a state", saved && !buf.bytes.empty(), msg);

            if (found && saved) {
                const double moved = before > 0.5 ? before - 0.4 : before + 0.4;
                clap_event_param_value_t e{};
                e.header.size = sizeof e;
                e.header.type = CLAP_EVENT_PARAM_VALUE;
                e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
                e.param_id = which;
                e.value = moved;
                e.note_id = -1; e.port_index = -1; e.channel = -1; e.key = -1;
                struct One {
                    static uint32_t CLAP_ABI size(const clap_input_events_t*) { return 1; }
                    static const clap_event_header_t* CLAP_ABI get(
                        const clap_input_events_t* l, uint32_t) {
                        return (const clap_event_header_t*)l->ctx;
                    }
                };
                clap_input_events_t one{&e, One::size, One::get};
                pars->flush(p, &one, &outEv);

                double after = 0;
                pars->get_value(p, which, &after);

                clap_istream_t is{&buf, Buf::rd};
                const bool loaded = st->load(p, &is);
                double back = 0;
                pars->get_value(p, which, &back);
                snprintf(msg, sizeof msg, "was %.2f, moved to %.2f, came back %.2f",
                         before, after, back);
                ok("and loading puts the parameter back where it was",
                   loaded && std::fabs(back - before) < 1e-3, msg);
            }
        }
    }

    // ---- the window, in a real X11 frame ----
    const clap_plugin_gui_t* gui =
        (const clap_plugin_gui_t*)p->get_extension(p, CLAP_EXT_GUI);
    ok("it offers a GUI", gui != nullptr);

    if (gui) {
        ok("on X11", gui->is_api_supported(p, CLAP_WINDOW_API_X11, false));
        ok("and creates one", gui->create(p, CLAP_WINDOW_API_X11, false));

        uint32_t w = 0, h = 0;
        gui->get_size(p, &w, &h);
        snprintf(msg, sizeof msg, "%u x %u", w, h);
        ok("with a size to open at", w > 0 && h > 0, msg);

        Display* dpy = XOpenDisplay(nullptr);
        ok("an X display is available", dpy != nullptr);
        if (dpy) {
            Window root = DefaultRootWindow(dpy);
            Window win = XCreateSimpleWindow(dpy, root, 0, 0, w ? w : 800, h ? h : 600,
                                             0, 0, 0x101010);
            XStoreName(dpy, win, "clap host");
            XMapWindow(dpy, win);
            XFlush(dpy);

            clap_window_t cw{};
            cw.api = CLAP_WINDOW_API_X11;
            cw.x11 = win;
            ok("the plug-in attaches to it", gui->set_parent(p, &cw));
            gui->show(p);
            XFlush(dpy);

            // The run loop under test: whatever the editor registered gets
            // serviced for a while, and it has to survive it.
            pump(seconds);
            snprintf(msg, sizeof msg, "%zu timers, %zu file descriptors",
                     g.timers.size(), g.fds.size());
            ok("and asks the host to drive it", !g.timers.empty() || !g.fds.empty(), msg);

            printf("       window id 0x%lx - capture it now if you want to look\n", win);
            pump(0.4);

            if (pressing && next != CLAP_INVALID_ID && voice != CLAP_INVALID_ID) {
                const int a = voiceNow();
                press(next);
                snprintf(msg, sizeof msg, "voice %d, then %d", a + 1, voiceNow() + 1);
                ok("with the editor open, its timer takes the press", voiceNow() == a + 1, msg);
            }

            // Close it and open it again, which is what a person does and
            // what the first version of this test never tried.
            gui->destroy(p);
            pump(0.2);

            const bool again = gui->create(p, CLAP_WINDOW_API_X11, false);
            ok("it opens a second time", again);
            if (again) {
                uint32_t w2 = 0, h2 = 0;
                gui->get_size(p, &w2, &h2);
                Window win2 = XCreateSimpleWindow(dpy, root, 0, 0, w2 ? w2 : 800,
                                                  h2 ? h2 : 600, 0, 0, 0x101010);
                XMapWindow(dpy, win2);
                XFlush(dpy);
                clap_window_t cw2{};
                cw2.api = CLAP_WINDOW_API_X11;
                cw2.x11 = win2;
                ok("and attaches to a new window", gui->set_parent(p, &cw2));
                gui->show(p);
                pump(seconds);
                ok("and survives being driven again", true);
                gui->destroy(p);
                XDestroyWindow(dpy, win2);
            }

            XDestroyWindow(dpy, win);
            XCloseDisplay(dpy);
            ok("the editor closes without taking the process with it", true);
        }
    }

    p->stop_processing(p);
    p->deactivate(p);
    p->destroy(p);
    entry->deinit();

    printf("\n%s\n", fails ? "FAILURES" : "all good");
    return fails ? 1 : 0;
}
