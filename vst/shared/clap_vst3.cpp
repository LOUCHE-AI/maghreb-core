#include "clap_vst3.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/parameterchanges.h"
#include "pluginterfaces/base/funknownimpl.h"
#include "pluginterfaces/gui/iplugview.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstcomponent.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

// Defined by the SDK's linuxmain.cpp, which is already in this module because
// the VST3 component's own sources are.
extern "C" bool ModuleEntry(void*);
extern "C" bool ModuleExit();

namespace mclap {
namespace {

std::string toAscii(const TChar* s) {
    std::string out;
    for (int i = 0; i < 128 && s[i]; i++) out.push_back((char)(s[i] < 128 ? s[i] : '?'));
    return out;
}

struct Wrapper;

// ---------------------------------------------------------------- edits
//
// A knob moved in the editor arrives here, on the UI thread, and has to reach
// the host as a parameter change. process() drains it on the audio thread, so
// it is a ring with two atomics rather than a list behind a lock: the audio
// thread is not a place to wait for the UI.
class EditQueue {
public:
    void push(ParamID id, ParamValue v) {
        const uint32_t w = write_.load(std::memory_order_relaxed);
        const uint32_t next = (w + 1) % kSize;
        if (next == read_.load(std::memory_order_acquire)) return;   // full: drop
        items_[w] = {id, v};
        write_.store(next, std::memory_order_release);
    }
    bool pop(ParamID& id, ParamValue& v) {
        const uint32_t r = read_.load(std::memory_order_relaxed);
        if (r == write_.load(std::memory_order_acquire)) return false;
        id = items_[r].id;
        v = items_[r].value;
        read_.store((r + 1) % kSize, std::memory_order_release);
        return true;
    }
private:
    struct Item { ParamID id; ParamValue value; };
    static const uint32_t kSize = 256;
    Item items_[kSize]{};
    std::atomic<uint32_t> read_{0}, write_{0};
};

// ------------------------------------------------------------ run loop
//
// VSTGUI's X11 backend needs somewhere to put its file descriptor and its
// timers. CLAP hands both of those to the plug-in as host extensions, so this
// is a thin translation - and, unlike the SDK's, it belongs to one instance and
// dies with it.
class PlugFrame : public U::Implements<U::Directly<IPlugFrame, Linux::IRunLoop>> {
public:
    PlugFrame(Wrapper* owner) : owner_(owner) {}

    tresult PLUGIN_API resizeView(IPlugView* view, ViewRect* rect) override;

    // --- Linux::IRunLoop, forwarded to the host ---
    tresult PLUGIN_API registerEventHandler(Linux::IEventHandler* handler,
                                            Linux::FileDescriptor fd) override;
    tresult PLUGIN_API unregisterEventHandler(Linux::IEventHandler* handler) override;
    tresult PLUGIN_API registerTimer(Linux::ITimerHandler* handler,
                                     Linux::TimerInterval ms) override;
    tresult PLUGIN_API unregisterTimer(Linux::ITimerHandler* handler) override;

    void onFd(int fd);
    void onTimer(clap_id id);

private:
    Wrapper* owner_;
    std::map<int, Linux::IEventHandler*> fds_;
    std::map<clap_id, Linux::ITimerHandler*> timers_;
};

// ------------------------------------------------------------- handler
class Handler : public U::Implements<U::Directly<IComponentHandler>> {
public:
    Handler(EditQueue* q) : q_(q) {}
    tresult PLUGIN_API beginEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue v) override {
        q_->push(id, v);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32) override { return kResultOk; }
private:
    EditQueue* q_;
};

// ------------------------------------------------------------- wrapper
struct Wrapper {
    clap_plugin_t plugin{};
    const clap_host_t* host = nullptr;
    const Vst3Spec* spec = nullptr;

    HostApplication hostApp;
    EditQueue edits;
    IPtr<Handler> handler;

    IPtr<IComponent> comp;
    IPtr<IAudioProcessor> proc;
    IPtr<IEditController> ctrl;

    std::vector<ParamID> paramIds;      // CLAP index -> VST3 id
    ParameterChanges inChanges, outChanges;
    EventList inEvents, outEvents;

    IPtr<IPlugView> view;
    IPtr<PlugFrame> frame;
    uint32_t latency = 0;
    double sampleRate = 48000.0;
    bool active = false;

    const clap_host_timer_support_t* timers = nullptr;
    const clap_host_posix_fd_support_t* fds = nullptr;
    const clap_host_gui_t* hostGui = nullptr;
};

Wrapper* self(const clap_plugin_t* p) { return static_cast<Wrapper*>(p->plugin_data); }

tresult PLUGIN_API PlugFrame::resizeView(IPlugView* view, ViewRect* rect) {
    if (!view || !rect) return kInvalidArgument;
    if (owner_->hostGui && owner_->host)
        owner_->hostGui->request_resize(owner_->host,
                                        (uint32_t)(rect->right - rect->left),
                                        (uint32_t)(rect->bottom - rect->top));
    return view->onSize(rect);
}

tresult PLUGIN_API PlugFrame::registerEventHandler(Linux::IEventHandler* handler,
                                                   Linux::FileDescriptor fd) {
    if (!handler || !owner_->fds) return kResultFalse;
    fds_[(int)fd] = handler;
    return owner_->fds->register_fd(owner_->host, (int)fd, CLAP_POSIX_FD_READ)
               ? kResultOk : kResultFalse;
}

tresult PLUGIN_API PlugFrame::unregisterEventHandler(Linux::IEventHandler* handler) {
    for (auto it = fds_.begin(); it != fds_.end(); ++it) {
        if (it->second != handler) continue;
        if (owner_->fds) owner_->fds->unregister_fd(owner_->host, it->first);
        fds_.erase(it);
        return kResultOk;
    }
    return kResultFalse;
}

tresult PLUGIN_API PlugFrame::registerTimer(Linux::ITimerHandler* handler,
                                            Linux::TimerInterval ms) {
    if (!handler || !owner_->timers) return kResultFalse;
    clap_id id = CLAP_INVALID_ID;
    if (!owner_->timers->register_timer(owner_->host, (uint32_t)(ms ? ms : 16), &id))
        return kResultFalse;
    timers_[id] = handler;
    return kResultOk;
}

tresult PLUGIN_API PlugFrame::unregisterTimer(Linux::ITimerHandler* handler) {
    for (auto it = timers_.begin(); it != timers_.end(); ++it) {
        if (it->second != handler) continue;
        if (owner_->timers) owner_->timers->unregister_timer(owner_->host, it->first);
        timers_.erase(it);
        return kResultOk;
    }
    return kResultFalse;
}

void PlugFrame::onFd(int fd) {
    auto it = fds_.find(fd);
    if (it != fds_.end() && it->second) it->second->onFDIsSet(fd);
}

void PlugFrame::onTimer(clap_id id) {
    auto it = timers_.find(id);
    if (it != timers_.end() && it->second) it->second->onTimer();
}

// ------------------------------------------------------------- params
uint32_t CLAP_ABI paramsCount(const clap_plugin_t* p) {
    return (uint32_t)self(p)->paramIds.size();
}

bool CLAP_ABI paramsInfo(const clap_plugin_t* p, uint32_t index, clap_param_info_t* out) {
    Wrapper* w = self(p);
    if (!w->ctrl || index >= w->paramIds.size()) return false;
    ParameterInfo info{};
    if (w->ctrl->getParameterInfo((int32)index, info) != kResultOk) return false;

    std::memset(out, 0, sizeof *out);
    out->id = info.id;
    // Normalised, as VST3 counts them. The reading a person sees comes from the
    // plug-in's own getParamStringByValue below, so nothing is lost by it.
    out->min_value = 0.0;
    out->max_value = 1.0;
    out->default_value = info.defaultNormalizedValue;
    out->flags = 0;
    if (info.flags & ParameterInfo::kCanAutomate)
        out->flags |= CLAP_PARAM_IS_AUTOMATABLE | CLAP_PARAM_IS_MODULATABLE;
    if (info.flags & ParameterInfo::kIsReadOnly) out->flags |= CLAP_PARAM_IS_READONLY;
    if (info.stepCount > 0) out->flags |= CLAP_PARAM_IS_STEPPED;
    snprintf(out->name, sizeof out->name, "%s", toAscii(info.title).c_str());
    snprintf(out->module, sizeof out->module, "%s", toAscii(info.units).c_str());
    return true;
}

bool CLAP_ABI paramsValue(const clap_plugin_t* p, clap_id id, double* out) {
    Wrapper* w = self(p);
    if (!w->ctrl) return false;
    *out = w->ctrl->getParamNormalized(id);
    return true;
}

bool CLAP_ABI paramsText(const clap_plugin_t* p, clap_id id, double value,
                         char* out, uint32_t size) {
    Wrapper* w = self(p);
    if (!w->ctrl) return false;
    String128 str{};
    if (w->ctrl->getParamStringByValue(id, value, str) != kResultOk) return false;
    snprintf(out, size, "%s", toAscii(str).c_str());
    return true;
}

bool CLAP_ABI paramsFromText(const clap_plugin_t* p, clap_id id, const char* text,
                             double* out) {
    Wrapper* w = self(p);
    if (!w->ctrl || !text) return false;
    TChar wide[128]{};
    int i = 0;
    for (; text[i] && i < 127; i++) wide[i] = (TChar)text[i];
    wide[i] = 0;
    ParamValue v = 0;
    if (w->ctrl->getParamValueByString(id, wide, v) != kResultOk) return false;
    *out = v;
    return true;
}

void takeEvents(Wrapper* w, const clap_input_events_t* in) {
    w->inChanges.clearQueue();
    w->inEvents.clear();
    if (!in) return;

    const uint32_t n = in->size(in);
    for (uint32_t i = 0; i < n; i++) {
        const clap_event_header_t* h = in->get(in, i);
        if (h->space_id != CLAP_CORE_EVENT_SPACE_ID) continue;

        if (h->type == CLAP_EVENT_PARAM_VALUE) {
            const clap_event_param_value_t* e = (const clap_event_param_value_t*)h;
            int32 index = 0;
            if (IParamValueQueue* q = w->inChanges.addParameterData(e->param_id, index)) {
                int32 pt = 0;
                q->addPoint((int32)h->time, e->value, pt);
            }
            // The editor draws from the controller, so it has to hear it too.
            if (w->ctrl) w->ctrl->setParamNormalized(e->param_id, e->value);
        } else if (h->type == CLAP_EVENT_NOTE_ON || h->type == CLAP_EVENT_NOTE_OFF) {
            const clap_event_note_t* e = (const clap_event_note_t*)h;
            Event ev{};
            ev.busIndex = 0;
            ev.sampleOffset = (int32)h->time;
            ev.flags = Event::kIsLive;
            if (h->type == CLAP_EVENT_NOTE_ON) {
                ev.type = Event::kNoteOnEvent;
                ev.noteOn.channel = (int16)(e->channel < 0 ? 0 : e->channel);
                ev.noteOn.pitch = (int16)e->key;
                ev.noteOn.velocity = (float)e->velocity;
                ev.noteOn.noteId = e->note_id;
            } else {
                ev.type = Event::kNoteOffEvent;
                ev.noteOff.channel = (int16)(e->channel < 0 ? 0 : e->channel);
                ev.noteOff.pitch = (int16)e->key;
                ev.noteOff.velocity = (float)e->velocity;
                ev.noteOff.noteId = e->note_id;
            }
            w->inEvents.addEvent(ev);
        }
    }
}

// Anything the editor changed, on its way to the host.
void sendEdits(Wrapper* w, const clap_output_events_t* out) {
    if (!out) return;
    ParamID id = 0;
    ParamValue v = 0;
    while (w->edits.pop(id, v)) {
        clap_event_param_value_t e{};
        e.header.size = sizeof e;
        e.header.type = CLAP_EVENT_PARAM_VALUE;
        e.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
        e.header.time = 0;
        e.param_id = id;
        e.value = v;
        e.note_id = -1;
        e.port_index = -1;
        e.channel = -1;
        e.key = -1;
        out->try_push(out, &e.header);
    }
}

void CLAP_ABI paramsFlush(const clap_plugin_t* p, const clap_input_events_t* in,
                          const clap_output_events_t* out) {
    Wrapper* w = self(p);
    takeEvents(w, in);
    if (w->proc && w->inChanges.getParameterCount() > 0) {
        // Not processing, so the change is handed over with no audio attached.
        ProcessData data{};
        data.numSamples = 0;
        data.symbolicSampleSize = kSample32;
        data.inputParameterChanges = &w->inChanges;
        data.outputParameterChanges = &w->outChanges;
        w->proc->process(data);
    }
    sendEdits(w, out);
}

const clap_plugin_params_t kParams = {
    paramsCount, paramsInfo, paramsValue, paramsText, paramsFromText, paramsFlush,
};

// -------------------------------------------------------------- ports
uint32_t CLAP_ABI portsCount(const clap_plugin_t* p, bool isInput) {
    return (isInput && !self(p)->spec->audioIn) ? 0 : 1;
}

bool CLAP_ABI portsGet(const clap_plugin_t* p, uint32_t index, bool isInput,
                       clap_audio_port_info_t* info) {
    if (index != 0 || (isInput && !self(p)->spec->audioIn)) return false;
    info->id = isInput ? 0 : 1;
    snprintf(info->name, sizeof info->name, "%s", isInput ? "In" : "Out");
    info->flags = CLAP_AUDIO_PORT_IS_MAIN;
    info->channel_count = 2;
    info->port_type = CLAP_PORT_STEREO;
    info->in_place_pair = CLAP_INVALID_ID;
    return true;
}

const clap_plugin_audio_ports_t kPorts = {portsCount, portsGet};

uint32_t CLAP_ABI notesCount(const clap_plugin_t* p, bool isInput) {
    const Vst3Spec* s = self(p)->spec;
    return (isInput ? s->noteIn : s->noteOut) ? 1 : 0;
}

bool CLAP_ABI notesGet(const clap_plugin_t* p, uint32_t index, bool isInput,
                       clap_note_port_info_t* info) {
    const Vst3Spec* s = self(p)->spec;
    if (index != 0 || (isInput ? !s->noteIn : !s->noteOut)) return false;
    info->id = isInput ? 0 : 1;
    info->supported_dialects = CLAP_NOTE_DIALECT_CLAP | CLAP_NOTE_DIALECT_MIDI;
    info->preferred_dialect = CLAP_NOTE_DIALECT_CLAP;
    snprintf(info->name, sizeof info->name, "Notes");
    return true;
}

const clap_plugin_note_ports_t kNotes = {notesCount, notesGet};

uint32_t CLAP_ABI latencyGet(const clap_plugin_t* p) { return self(p)->latency; }
const clap_plugin_latency_t kLatency = {latencyGet};

// -------------------------------------------------------------- state
bool CLAP_ABI stateSave(const clap_plugin_t* p, const clap_ostream_t* os) {
    Wrapper* w = self(p);
    if (!w->comp) return false;
    MemoryStream mem;
    if (w->comp->getState(&mem) != kResultOk) return false;

    const int64 size = mem.getSize();
    const int32_t n = (int32_t)size;
    if (os->write(os, &n, sizeof n) != sizeof n) return false;
    const char* data = mem.getData();
    int32_t done = 0;
    while (done < n) {
        const int64_t wrote = os->write(os, data + done, (uint64_t)(n - done));
        if (wrote <= 0) return false;
        done += (int32_t)wrote;
    }
    return true;
}

bool CLAP_ABI stateLoad(const clap_plugin_t* p, const clap_istream_t* is) {
    Wrapper* w = self(p);
    if (!w->comp) return false;
    int32_t n = 0;
    if (is->read(is, &n, sizeof n) != sizeof n || n < 0) return false;

    std::vector<char> buf((size_t)n);
    int32_t done = 0;
    while (done < n) {
        const int64_t got = is->read(is, buf.data() + done, (uint64_t)(n - done));
        if (got <= 0) return false;
        done += (int32_t)got;
    }
    MemoryStream mem(buf.data(), n);
    mem.seek(0, IBStream::kIBSeekSet, nullptr);
    if (w->comp->setState(&mem) != kResultOk) return false;

    // The controller keeps its own copy of the state, and the editor draws
    // from the controller.
    if (w->ctrl) {
        mem.seek(0, IBStream::kIBSeekSet, nullptr);
        w->ctrl->setComponentState(&mem);
    }
    return true;
}

const clap_plugin_state_t kState = {stateSave, stateLoad};

// ---------------------------------------------------------------- gui
bool CLAP_ABI guiIsApiSupported(const clap_plugin_t*, const char* api, bool floating) {
    return !floating && api && !std::strcmp(api, CLAP_WINDOW_API_X11);
}

bool CLAP_ABI guiGetPreferredApi(const clap_plugin_t*, const char** api, bool* floating) {
    *api = CLAP_WINDOW_API_X11;
    *floating = false;
    return true;
}

bool CLAP_ABI guiCreate(const clap_plugin_t* p, const char* api, bool floating) {
    Wrapper* w = self(p);
    if (floating || !api || std::strcmp(api, CLAP_WINDOW_API_X11) || !w->ctrl) return false;
    if (w->view) return true;

    w->view = owned(w->ctrl->createView(ViewType::kEditor));
    if (!w->view) return false;
    if (w->view->isPlatformTypeSupported(kPlatformTypeX11EmbedWindowID) != kResultTrue) {
        w->view = nullptr;
        return false;
    }
    w->frame = owned(new PlugFrame(w));
    w->view->setFrame(w->frame.get());
    return true;
}

void CLAP_ABI guiDestroy(const clap_plugin_t* p) {
    Wrapper* w = self(p);
    if (!w->view) return;
    w->view->removed();
    w->view->setFrame(nullptr);
    w->view = nullptr;
    w->frame = nullptr;
}

bool CLAP_ABI guiSetScale(const clap_plugin_t*, double) { return false; }

bool CLAP_ABI guiGetSize(const clap_plugin_t* p, uint32_t* width, uint32_t* height) {
    Wrapper* w = self(p);
    if (!w->view) return false;
    ViewRect r{};
    if (w->view->getSize(&r) != kResultOk) return false;
    *width = (uint32_t)(r.right - r.left);
    *height = (uint32_t)(r.bottom - r.top);
    return true;
}

bool CLAP_ABI guiCanResize(const clap_plugin_t* p) {
    Wrapper* w = self(p);
    return w->view && w->view->canResize() == kResultTrue;
}

bool CLAP_ABI guiGetResizeHints(const clap_plugin_t*, clap_gui_resize_hints_t*) {
    return false;
}

bool CLAP_ABI guiAdjustSize(const clap_plugin_t* p, uint32_t* w_, uint32_t* h_) {
    Wrapper* w = self(p);
    if (!w->view) return false;
    ViewRect r{0, 0, (int32)*w_, (int32)*h_};
    if (w->view->checkSizeConstraint(&r) != kResultTrue) return false;
    *w_ = (uint32_t)(r.right - r.left);
    *h_ = (uint32_t)(r.bottom - r.top);
    return true;
}

bool CLAP_ABI guiSetSize(const clap_plugin_t* p, uint32_t width, uint32_t height) {
    Wrapper* w = self(p);
    if (!w->view) return false;
    ViewRect r{0, 0, (int32)width, (int32)height};
    return w->view->onSize(&r) == kResultOk;
}

bool CLAP_ABI guiSetParent(const clap_plugin_t* p, const clap_window_t* window) {
    Wrapper* w = self(p);
    if (!w->view || !window) return false;
    return w->view->attached((void*)(uintptr_t)window->x11,
                             kPlatformTypeX11EmbedWindowID) == kResultOk;
}

bool CLAP_ABI guiSetTransient(const clap_plugin_t*, const clap_window_t*) { return false; }
void CLAP_ABI guiSuggestTitle(const clap_plugin_t*, const char*) {}
bool CLAP_ABI guiShow(const clap_plugin_t*) { return true; }
bool CLAP_ABI guiHide(const clap_plugin_t*) { return true; }

const clap_plugin_gui_t kGui = {
    guiIsApiSupported, guiGetPreferredApi, guiCreate, guiDestroy, guiSetScale,
    guiGetSize, guiCanResize, guiGetResizeHints, guiAdjustSize, guiSetSize,
    guiSetParent, guiSetTransient, guiSuggestTitle, guiShow, guiHide,
};

// The host's timers and file descriptors are what drive the editor.
void CLAP_ABI onTimer(const clap_plugin_t* p, clap_id id) {
    if (self(p)->frame) self(p)->frame->onTimer(id);
}
const clap_plugin_timer_support_t kTimerSupport = {onTimer};

void CLAP_ABI onFd(const clap_plugin_t* p, int fd, clap_posix_fd_flags_t) {
    if (self(p)->frame) self(p)->frame->onFd(fd);
}
const clap_plugin_posix_fd_support_t kFdSupport = {onFd};

// ------------------------------------------------------------- plug-in
bool CLAP_ABI plugInit(const clap_plugin_t* p) {
    Wrapper* w = self(p);
    if (w->host) {
        w->timers = (const clap_host_timer_support_t*)
            w->host->get_extension(w->host, CLAP_EXT_TIMER_SUPPORT);
        w->fds = (const clap_host_posix_fd_support_t*)
            w->host->get_extension(w->host, CLAP_EXT_POSIX_FD_SUPPORT);
        w->hostGui = (const clap_host_gui_t*)
            w->host->get_extension(w->host, CLAP_EXT_GUI);
    }

    FUnknown* raw = w->spec->createInstance(nullptr);
    if (!raw) return false;
    w->proc = owned(FUnknownPtr<IAudioProcessor>(raw).getInterface());
    w->comp = FUnknownPtr<IComponent>(raw);
    w->ctrl = FUnknownPtr<IEditController>(raw);
    if (!w->comp || !w->proc || !w->ctrl) return false;

    if (w->comp->initialize(static_cast<FUnknown*>(&w->hostApp)) != kResultOk) return false;
    w->handler = owned(new Handler(&w->edits));
    w->ctrl->setComponentHandler(w->handler);

    const int32 count = w->ctrl->getParameterCount();
    for (int32 i = 0; i < count; i++) {
        ParameterInfo info{};
        if (w->ctrl->getParameterInfo(i, info) == kResultOk) w->paramIds.push_back(info.id);
    }
    return true;
}

void CLAP_ABI plugDestroy(const clap_plugin_t* p) {
    Wrapper* w = self(p);
    guiDestroy(p);
    if (w->comp) {
        if (w->active) w->comp->setActive(false);
        w->ctrl->setComponentHandler(nullptr);
        w->comp->terminate();
    }
    delete w;
}

bool CLAP_ABI plugActivate(const clap_plugin_t* p, double sampleRate,
                           uint32_t, uint32_t maxFrames) {
    Wrapper* w = self(p);
    w->sampleRate = sampleRate;

    // What a VST3 host does before it may set the component active: say what
    // the buses carry, then say which of them are in use. Skipping either is
    // how setActive comes back with an error and no explanation.
    SpeakerArrangement stereo = SpeakerArr::kStereo;
    if (w->spec->audioIn)
        w->proc->setBusArrangements(&stereo, 1, &stereo, 1);
    else
        w->proc->setBusArrangements(nullptr, 0, &stereo, 1);

    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = (int32)maxFrames;
    setup.sampleRate = sampleRate;
    if (w->proc->setupProcessing(setup) != kResultOk) return false;

    for (int32 i = 0; i < w->comp->getBusCount(kAudio, kInput); i++)
        w->comp->activateBus(kAudio, kInput, i, true);
    for (int32 i = 0; i < w->comp->getBusCount(kAudio, kOutput); i++)
        w->comp->activateBus(kAudio, kOutput, i, true);
    for (int32 i = 0; i < w->comp->getBusCount(kEvent, kInput); i++)
        w->comp->activateBus(kEvent, kInput, i, true);
    for (int32 i = 0; i < w->comp->getBusCount(kEvent, kOutput); i++)
        w->comp->activateBus(kEvent, kOutput, i, true);

    if (w->comp->setActive(true) != kResultOk) return false;
    w->active = true;
    w->latency = w->proc->getLatencySamples();
    return true;
}

void CLAP_ABI plugDeactivate(const clap_plugin_t* p) {
    Wrapper* w = self(p);
    if (!w->active) return;
    w->comp->setActive(false);
    w->active = false;
}

bool CLAP_ABI plugStart(const clap_plugin_t* p) {
    // A component that does not implement setProcessing is not a component
    // that failed to start: most of these do not need to know.
    const tresult r = self(p)->proc->setProcessing(true);
    return r == kResultOk || r == kNotImplemented;
}
void CLAP_ABI plugStop(const clap_plugin_t* p) { self(p)->proc->setProcessing(false); }
void CLAP_ABI plugReset(const clap_plugin_t*) {}

clap_process_status CLAP_ABI plugProcess(const clap_plugin_t* p,
                                         const clap_process_t* pr) {
    Wrapper* w = self(p);
    if (!w->proc || pr->audio_outputs_count < 1) return CLAP_PROCESS_CONTINUE;

    takeEvents(w, pr->in_events);
    w->outChanges.clearQueue();
    w->outEvents.clear();

    AudioBusBuffers inBus{}, outBus{};
    outBus.numChannels = (int32)pr->audio_outputs[0].channel_count;
    outBus.channelBuffers32 = pr->audio_outputs[0].data32;
    if (w->spec->audioIn && pr->audio_inputs_count > 0) {
        inBus.numChannels = (int32)pr->audio_inputs[0].channel_count;
        inBus.channelBuffers32 = pr->audio_inputs[0].data32;
    }

    ProcessContext ctx{};
    ctx.sampleRate = w->sampleRate;
    ctx.state = ProcessContext::kTempoValid | ProcessContext::kProjectTimeMusicValid;
    ctx.tempo = 120.0;
    if (pr->transport) {
        ctx.tempo = (double)pr->transport->tempo;
        ctx.projectTimeMusic =
            (double)pr->transport->song_pos_beats / (double)CLAP_BEATTIME_FACTOR;
        if (pr->transport->flags & CLAP_TRANSPORT_IS_PLAYING)
            ctx.state |= ProcessContext::kPlaying;
    }

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = (int32)pr->frames_count;
    data.numInputs = w->spec->audioIn ? 1 : 0;
    data.numOutputs = 1;
    data.inputs = w->spec->audioIn ? &inBus : nullptr;
    data.outputs = &outBus;
    data.inputParameterChanges = &w->inChanges;
    data.outputParameterChanges = &w->outChanges;
    data.inputEvents = &w->inEvents;
    data.outputEvents = w->spec->noteOut ? &w->outEvents : nullptr;
    data.processContext = &ctx;

    w->proc->process(data);

    // Whatever it played, on its way out as notes. The sequencer is the one
    // that uses this: the pattern it strikes leaves as MIDI as well as audio.
    if (w->spec->noteOut && pr->out_events) {
        const int32 count = w->outEvents.getEventCount();
        for (int32 i = 0; i < count; i++) {
            Event ev{};
            if (w->outEvents.getEvent(i, ev) != kResultOk) continue;
            if (ev.type != Event::kNoteOnEvent && ev.type != Event::kNoteOffEvent) continue;

            clap_event_note_t note{};
            note.header.size = sizeof note;
            note.header.time = (uint32_t)ev.sampleOffset;
            note.header.space_id = CLAP_CORE_EVENT_SPACE_ID;
            note.header.type = (ev.type == Event::kNoteOnEvent) ? CLAP_EVENT_NOTE_ON
                                                                : CLAP_EVENT_NOTE_OFF;
            note.port_index = 0;
            note.note_id = -1;
            if (ev.type == Event::kNoteOnEvent) {
                note.channel = ev.noteOn.channel;
                note.key = ev.noteOn.pitch;
                note.velocity = ev.noteOn.velocity;
            } else {
                note.channel = ev.noteOff.channel;
                note.key = ev.noteOff.pitch;
                note.velocity = ev.noteOff.velocity;
            }
            pr->out_events->try_push(pr->out_events, &note.header);
        }
    }

    sendEdits(w, pr->out_events);

    const uint32_t now = w->proc->getLatencySamples();
    if (now != w->latency) {
        w->latency = now;
        if (w->host) {
            const clap_host_latency_t* ext = (const clap_host_latency_t*)
                w->host->get_extension(w->host, CLAP_EXT_LATENCY);
            if (ext) ext->changed(w->host);
        }
    }
    return CLAP_PROCESS_CONTINUE;
}

const void* CLAP_ABI plugExtension(const clap_plugin_t* p, const char* id) {
    Wrapper* w = self(p);
    if (!std::strcmp(id, CLAP_EXT_AUDIO_PORTS))      return &kPorts;
    if (!std::strcmp(id, CLAP_EXT_PARAMS))           return &kParams;
    if (!std::strcmp(id, CLAP_EXT_STATE))            return &kState;
    if (!std::strcmp(id, CLAP_EXT_GUI))              return &kGui;
    if (!std::strcmp(id, CLAP_EXT_TIMER_SUPPORT))    return &kTimerSupport;
    if (!std::strcmp(id, CLAP_EXT_POSIX_FD_SUPPORT)) return &kFdSupport;
    if (!std::strcmp(id, CLAP_EXT_LATENCY))          return &kLatency;
    if (!std::strcmp(id, CLAP_EXT_NOTE_PORTS) && (w->spec->noteIn || w->spec->noteOut))
        return &kNotes;
    return nullptr;
}

void CLAP_ABI plugOnMainThread(const clap_plugin_t*) {}

} // namespace

bool moduleInit(const char*) {
    // No handle, and deliberately no dlopen to find one.
    //
    // The SDK only wants it to locate files shipped beside the binary, and
    // these plug-ins ship none - they draw their interfaces in code. Asking
    // dlopen for a handle to the module we are already inside is what took
    // Bitwig's scanner down: it loads plug-ins on their own link map, and a
    // module calling back into the loader from there does not survive it.
    return ModuleEntry(nullptr);
}

void moduleDeinit() { ModuleExit(); }

const clap_plugin_t* make(const Vst3Spec& spec, const clap_host_t* host) {
    Wrapper* w = new Wrapper();
    w->host = host;
    w->spec = &spec;
    w->plugin.desc = spec.desc;
    w->plugin.plugin_data = w;
    w->plugin.init = plugInit;
    w->plugin.destroy = plugDestroy;
    w->plugin.activate = plugActivate;
    w->plugin.deactivate = plugDeactivate;
    w->plugin.start_processing = plugStart;
    w->plugin.stop_processing = plugStop;
    w->plugin.reset = plugReset;
    w->plugin.process = plugProcess;
    w->plugin.get_extension = plugExtension;
    w->plugin.on_main_thread = plugOnMainThread;
    return &w->plugin;
}

} // namespace mclap
