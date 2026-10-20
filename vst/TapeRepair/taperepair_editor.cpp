#include "taperepair_editor.h"
#include "taperepair_vst.h"
#include "vstgui/lib/cvstguitimer.h"
#include "platform.h"
#include "frame_open.h"
#include "ui_scale.h"
#include "audio_import.h"
#include "vstgui/lib/cfileselector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

using namespace VSTGUI;

namespace Steinberg {
namespace Vst {

// The family's colours.
static const CColor kBack ( 12,  12,  14, 255);
static const CColor kCell ( 17,  17,  22, 255);
static const CColor kLine ( 22,  23,  28, 255);
static const CColor kInk  (214, 210, 222, 255);
static const CColor kLab  (138, 133, 152, 255);
static const CColor kDim  ( 87,  84,  95, 255);
static const CColor kTape (120, 190, 235, 255);   // what came off the tape
static const CColor kFixed(224, 163,  60, 255);   // what the repair made of it
static const CColor kHead (255, 245, 225, 255);

static mui::Palette palette() {
    return mui::Palette{kBack, CColor(27, 28, 34, 255), CColor(17, 17, 23, 255),
                        CColor(20, 21, 26, 255), kInk, kLab, kDim};
}

static const int kEdW   = 820;
static const int kMar   = 20;
static const int kTopY  = 14;
static const int kWaveY = 44;
static const int kWaveH = 190;
static const int kTranY = kWaveY + kWaveH + 14;
static const int kRowY  = kTranY + 32;
static const int kRowH  = 58;
static const int kActY  = kRowY + kRowH + 10;
static const int kHelpY = kActY + 32;
static const int kEdH   = kHelpY + 40;

// ------------------------------------------------------------- waveform

void WaveView::draw(CDrawContext* c) {
    const CRect r = getViewSize();
    c->setDrawMode(kAntiAliasing);
    c->setFillColor(pal_.cell);
    c->drawRect(r, kDrawFilled);

    const CCoord mid = r.top + r.getHeight() / 2;
    c->setFrameColor(pal_.rule);
    c->drawLine(CPoint(r.left, mid), CPoint(r.right, mid));

    if (tape_.hi.empty()) {
        c->setFont(nullptr);
        c->setFontColor(pal_.dim);
        c->drawString("arm, and play the tape in",
                      CRect(r.left, mid - 10, r.right, mid + 10), kCenterText);
        return;
    }

    const CCoord half = r.getHeight() / 2 - 4;
    const size_t n = tape_.hi.size();
    const CCoord step = r.getWidth() / (CCoord)n;

    // the region LEARN will measure from, behind everything else
    if (learnAt_ >= 0.0 && learnW_ > 0.0) {
        const CCoord x0 = r.left + r.getWidth() * learnAt_;
        const CCoord x1 = std::min(r.right, x0 + r.getWidth() * learnW_);
        c->setFillColor(CColor(40, 44, 54, 255));
        c->drawRect(CRect(x0, r.top, x1, r.bottom), kDrawFilled);
    }

    // the tape underneath, the repair over it - so what the repair removed
    // shows as blue standing out past the orange
    auto drawPeaks = [&](const trep::TapeRepair::Peaks& p, const CColor& col) {
        if (p.hi.empty()) return;
        c->setFrameColor(col);
        for (size_t i = 0; i < p.hi.size(); i++) {
            const CCoord x = r.left + (CCoord)i * step;
            const CCoord hi = mid - p.hi[i] * half;
            const CCoord lo = mid - p.lo[i] * half;
            c->drawLine(CPoint(x, hi), CPoint(x, lo));
        }
    };
    drawPeaks(tape_, kTape);
    drawPeaks(fixed_, kFixed);

    if (head_ >= 0.0) {
        const CCoord x = r.left + r.getWidth() * head_;
        c->setFrameColor(kHead);
        c->drawLine(CPoint(x, r.top), CPoint(x, r.bottom));
    }
}

void WaveView::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft() || tape_.hi.empty()) return;
    const CRect r = getViewSize();
    const double f = std::max(0.0, std::min(1.0, (e.mousePosition.x - r.left) / r.getWidth()));
    if (pick_) pick_(f);
    e.consumed = true;
}

// --------------------------------------------------------------- editor

TapeRepairEditor::TapeRepairEditor(void* controller, TapeRepairVST* plug)
    : VSTGUIEditor(controller), plug_(plug) {
    // The size the host is told about, before it ever asks for the window.
    // Truncated, not rounded, to match what VSTGUIEditor::attached derives
    // from the zoomed frame.
    const double uiScale = mplat::editorScale();
    ViewRect vr(0, 0, (int32)(kEdW * uiScale), (int32)(kEdH * uiScale));
    setRect(vr);
}

CTextLabel* TapeRepairEditor::makeLabel(const CRect& r, const char* text,
                                        const CColor& col, int align, bool mono) {
    auto* l = new CTextLabel(r, text);
    l->setBackColor(kTransparentCColor);
    l->setFrameColor(kTransparentCColor);
    l->setFontColor(col);
    l->setFont(mono ? valueFont_ : labelFont_);
    l->setHoriAlign((CHoriTxtAlign)align);
    return l;
}

void TapeRepairEditor::styleButton(CTextButton* b) {
    b->setFont(labelFont_);
    b->setTextColor(kInk);
    b->setTextColorHighlighted(kBack);
    b->setFrameColor(kLine);
    b->setFrameColorHighlighted(kFixed);
    b->setGradient(CGradient::create(0, 1, kCell, kCell));
    b->setGradientHighlighted(CGradient::create(0, 1, kFixed, kFixed));
    b->setRoundRadius(2);
}

bool PLUGIN_API TapeRepairEditor::open(void* parent, const PlatformType& type) {
    CRect size(0, 0, kEdW, kEdH);
    frame = new CFrame(size, this);
    frame->setBackgroundColor(kBack);
    if (!mplat::openEditorFrame(frame, parent, type, plugFrame)) { frame = nullptr; return false; }

    // Fixed-pixel layout on a HiDPI panel is a postage stamp; scale the whole
    // frame rather than every coordinate in it. Must follow open(), which is
    // what creates the platform frame this resizes.
    frame->setZoom(mplat::editorScale());

    labelFont_ = VSTGUI::owned(new CFontDesc(mplat::kUiFont, 10));
    valueFont_ = VSTGUI::owned(new CFontDesc(mplat::kMonoFont, 11));
    bigFont_   = VSTGUI::owned(new CFontDesc(mplat::kMonoFont, 17));
    titleFont_ = VSTGUI::owned(new CFontDesc(mplat::kUiFont, 11, kBoldFace));

    frame->registerMouseObserver(this);

    // ---- title and status ----
    {
        auto* t = makeLabel(CRect(kMar, kTopY, kMar + 240, kTopY + 18),
                            "TAPE REPAIR", kInk, kLeftText);
        t->setFont(titleFont_);
        t->setMouseEnabled(false);
        frame->addView(t);

        status_ = makeLabel(CRect(kEdW - kMar - 460, kTopY, kEdW - kMar, kTopY + 18),
                            "", kDim, kRightText, true);
        status_->setMouseEnabled(false);
        frame->addView(status_);
    }

    // ---- waveform ----
    wave_ = new WaveView(CRect(kMar, kWaveY, kEdW - kMar, kWaveY + kWaveH), palette(),
                         [this](double f) {
                             learnAt_ = f;
                             if (wave_) wave_->setLearnAt(f, 0.08);
                             setHelp("LEARN will measure the floor from here. "
                                     "Pick a gap with no music in it.");
                         });
    frame->addView(wave_);

    // ---- transport ----
    {
        const int y = kTranY;
        auto* load = new CTextButton(CRect(kMar, y, kMar + 90, y + 23), this, kLoadTag, "LOAD");
        styleButton(load);
        load->setTooltipText("Opens a file as the take - a WAV directly, anything else "
                             "through ffmpeg. It is resampled to the session rate on "
                             "the way in.");
        frame->addView(load);

        armBtn_ = new CTextButton(CRect(kMar + 98, y, kMar + 188, y + 23), this, kArmTag,
                                  "ARM", CTextButton::kOnOffStyle);
        styleButton(armBtn_);
        armBtn_->setTooltipText("Records what comes in. Arming again starts a new take.");
        frame->addView(armBtn_);

        playBtn_ = new CTextButton(CRect(kMar + 196, y, kMar + 286, y + 23), this, kPlayTag,
                                   "PLAY", CTextButton::kOnOffStyle);
        styleButton(playBtn_);
        playBtn_->setTooltipText("Plays the take back into the track.");
        frame->addView(playBtn_);

        compareBtn_ = new CTextButton(CRect(kMar + 294, y, kMar + 414, y + 23), this,
                                      kCompareTag, "A/B", CTextButton::kOnOffStyle);
        styleButton(compareBtn_);
        compareBtn_->setTooltipText("Swaps between the repair and what came off the tape. "
                                    "The only honest way to judge any of this.");
        frame->addView(compareBtn_);

        followBtn_ = new CTextButton(CRect(kMar + 422, y, kMar + 530, y + 23), this,
                                   kFollowTag, "FOLLOW", CTextButton::kOnOffStyle);
        styleButton(followBtn_);
        followBtn_->setTooltipText(
            "Arms with the host's transport: play in the scene and the take "
            "starts, stop and it ends. Also how you get a track that is already "
            "in the project into here - play it at this.");
        frame->addView(followBtn_);

        auto* stop = new CTextButton(CRect(kMar + 538, y, kMar + 666, y + 23), this,
                                     kAutoStopTag, "AUTO-STOP", CTextButton::kOnOffStyle);
        styleButton(stop);
        stop->setTooltipText(
            "Ends the take ten seconds after the tape runs out, and drops that "
            "silence. Play the side in and walk away.");
        frame->addView(stop);
        autoStopBtn_ = stop;

        // Both are real parameters, so a reopened window shows what the
        // project already had rather than the default.
        followBtn_->setValue(plug_->normalizedOf(kFollowId) >= 0.5 ? 1.f : 0.f);
        autoStopBtn_->setValue(plug_->normalizedOf(kAutoStopId) >= 0.5 ? 1.f : 0.f);
    }

    // ---- the settings, as one row of draggable cells ----
    row_ = new mui::ParamRow(CRect(kMar, kRowY, kEdW - kMar, kRowY + kRowH), palette(),
                             labelFont_, bigFont_,
                             [this](int param, double delta, int phase) {
                                 if (!plug_ || param < 0) return;
                                 if (phase == 1) {
                                     const double v = std::max(0.0, std::min(1.0,
                                         plug_->normalizedOf(param) + delta));
                                     plug_->setParamFromUI(param, v);
                                     refreshCells();
                                 }
                             });
    frame->addView(row_);

    // ---- the four things to press ----
    {
        const int y = kActY;
        struct { int tag; const char* label; int w; const char* tip; } acts[] = {
            { kLearnTag,  "LEARN",  110, "Measures the noise floor. Click a gap in the "
                                         "waveform first, or let it find the quietest "
                                         "second by itself." },
            { kRepairTag, "REPAIR", 110, "Runs the whole chain on the take. The tape "
                                         "itself is never overwritten." },
            { kRevertTag, "REVERT", 110, "Throws the repair away and keeps the tape." },
            { kSendTag,   "SEND TO TAPEGRAIN", 190,
                                         "Writes what you are hearing into TapeGrain's "
                                         "samples folder." },
            { kSplitTag,  "SPLIT SIDE", 140,
                                         "Finds the songs on the side - a gap is two "
                                         "seconds under -45 dB - and writes each as its "
                                         "own file, next to the handoff. Splits what you "
                                         "are hearing, so REPAIR first if you want them "
                                         "mended." },
        };
        int x = kMar;
        for (const auto& a : acts) {
            auto* b = new CTextButton(CRect(x, y, x + a.w, y + 23), this, a.tag, a.label);
            styleButton(b);
            b->setTooltipText(a.tip);
            frame->addView(b);
            x += a.w + 8;
        }
    }

    // ---- help ----
    help_ = makeLabel(CRect(kMar, kHelpY, kEdW - kMar, kHelpY + 30), "", kLab, kLeftText);
    help_->setMouseEnabled(false);
    frame->addView(help_);
    setHelp(nullptr);

    frame->enableTooltips(true);
    refreshWave();
    refreshCells();
    refreshStatus();
    if (plug_) plug_->setEditor(this);
    return true;
}

void PLUGIN_API TapeRepairEditor::close() {
    if (plug_) plug_->setEditor(nullptr);
    if (frame) {
        frame->unregisterMouseObserver(this);
        frame->forget();
        frame = nullptr;
        mplat::closeEditorFrame();
    }
    wave_ = nullptr; row_ = nullptr; status_ = nullptr; help_ = nullptr;
    armBtn_ = playBtn_ = compareBtn_ = nullptr;
    followBtn_ = autoStopBtn_ = nullptr;
}

void TapeRepairEditor::setHelp(const char* text) {
    if (!help_) return;
    help_->setText(text && *text
        ? text
        : "Arm, play the side in, then LEARN on a gap and REPAIR. A/B tells you "
          "whether it helped. Hover anything for what it does.");
}

void TapeRepairEditor::refreshWave() {
    if (!wave_ || !plug_) return;
    const int columns = (int)(kEdW - 2 * kMar);
    wave_->setPeaks(plug_->engine().peaks(columns, false),
                    plug_->engine().hasRepair() ? plug_->engine().peaks(columns, true)
                                                : trep::TapeRepair::Peaks{});
}

void TapeRepairEditor::refreshCells() {
    if (!row_ || !plug_) return;
    // The seven that are worth a cell. Compare and Bypass are buttons and the
    // transport is its own row, so they are not here.
    static const int shown[] = { trep::kHum, trep::kHarmonics, trep::kDenoise,
                                 trep::kFloor, trep::kDcBlock, trep::kNormalize,
                                 trep::kOutput };
    std::vector<mui::ParamRow::Cell> cells;
    for (int id : shown) {
        mui::ParamRow::Cell c;
        c.param = id;
        c.label = kParams[id].name;
        c.norm = (float)plug_->normalizedOf(id);
        c.bar = kFixed;

        char buf[64];
        if (kParams[id].names) {
            const int k = (int)std::lround(kParams[id].lo +
                c.norm * (kParams[id].hi - kParams[id].lo));
            snprintf(buf, sizeof buf, "%s",
                     kParams[id].names[std::max(0, std::min(kParams[id].numNames - 1, k))]);
        } else {
            const double v = kParams[id].dispLo + c.norm * (kParams[id].dispHi - kParams[id].dispLo);
            if (id == trep::kNormalize && v <= trep::kNormOff + 0.5)
                snprintf(buf, sizeof buf, "off");
            else
                snprintf(buf, sizeof buf, "%.*f%s", kParams[id].precision, v, kParams[id].unit);
        }
        c.value = buf;
        cells.push_back(c);
    }
    row_->setCells(cells);
}

void TapeRepairEditor::refreshStatus() {
    if (!status_ || !plug_) return;
    const trep::TapeRepair& e = plug_->engine();
    char buf[160];
    // What the input is doing, for the level check that has to happen before
    // the tape rolls rather than after it.
    const float pk = e.inputPeak();
    char level[48];
    if (pk >= 0.999f)      snprintf(level, sizeof level, "IN CLIPPING");
    else if (pk > 1e-5f)   snprintf(level, sizeof level, "IN %.1f dB", 20.0 * std::log10(pk));
    else                   snprintf(level, sizeof level, "IN silent");

    if (e.working()) {
        snprintf(buf, sizeof buf, "REPAIRING   %.0f%%   (press REPAIR again to stop)",
                 e.progress() * 100.0);
    } else if (e.armed()) {
        snprintf(buf, sizeof buf, "RECORDING   %.1f s   %s", e.takeSeconds(), level);
    } else if (!e.hasTake()) {
        snprintf(buf, sizeof buf, "NO TAKE   %s", level);
    } else {
        const double moved = e.floorChangeDb();
        snprintf(buf, sizeof buf, "%.1f s   %s   %s%s",
                 e.takeSeconds(),
                 e.hasProfile() ? "floor learned" : "no floor yet",
                 e.hasRepair() ? "repaired" : "not repaired",
                 e.hasRepair() && moved < -0.1 ? "" : "");
        if (e.hasRepair() && moved < -0.1) {
            char m[48];
            snprintf(m, sizeof m, "  %+.1f dB floor", moved);
            strncat(buf, m, sizeof buf - strlen(buf) - 1);
        }
    }
    status_->setText(buf);
}

void TapeRepairEditor::valueChanged(CControl* c) {
    if (!plug_ || !c) return;
    const int32_t tag = c->getTag();

    // kick buttons fire twice
    if (c->getValue() < 0.5f &&
        (tag == kLearnTag || tag == kRepairTag || tag == kRevertTag ||
         tag == kSendTag  || tag == kLoadTag))
        return;

    switch (tag) {
        case kLoadTag: {
            auto* sel = CNewFileSelector::create(frame, CNewFileSelector::kSelectFile);
            if (!sel) return;
            sel->setTitle("Open an audio file");
            for (int i = 0; i < maudio::kNumExtensions; i++)
                sel->addFileExtension(CFileExtension(maudio::kExtensions[i],
                                                     maudio::kExtensions[i]));
            sel->run([this](CNewFileSelector* s) {
                if (s->getNumSelectedFiles() == 0 || !plug_) return;
                std::string err;
                if (plug_->loadFile(s->getSelectedFile(0), &err)) {
                    learnAt_ = -1.0;
                    if (wave_) wave_->setLearnAt(-1.0, 0.0);
                    setHelp("Loaded. LEARN on a gap with no music in it, then REPAIR.");
                } else {
                    setHelp(err.empty() ? "Could not read that file." : err.c_str());
                }
                if (armBtn_)  { armBtn_->setValue(0.f);  armBtn_->invalid(); }
                if (playBtn_) { playBtn_->setValue(0.f); playBtn_->invalid(); }
                refreshWave();
                refreshStatus();
            });
            sel->forget();
            return;
        }

        case kArmTag:
            plug_->setParamFromUI(kArmId, c->getValue() >= 0.5f ? 1.0 : 0.0);
            setHelp(c->getValue() >= 0.5f ? "Recording. Press ARM again when the side ends."
                                          : "Take kept. LEARN on a gap, then REPAIR.");
            refreshWave();
            break;

        case kPlayTag:
            plug_->setParamFromUI(kPlayId, c->getValue() >= 0.5f ? 1.0 : 0.0);
            break;

        case kFollowTag:
            plug_->setParamFromUI(kFollowId, c->getValue() >= 0.5f ? 1.0 : 0.0);
            setHelp(c->getValue() >= 0.5f
                ? "Following the host. Press play in the scene to start the take."
                : "Not following. ARM records.");
            break;

        case kAutoStopTag:
            plug_->setParamFromUI(kAutoStopId, c->getValue() >= 0.5f ? 1.0 : 0.0);
            setHelp(c->getValue() >= 0.5f
                ? "The take will end itself ten seconds after the tape runs out."
                : "The take runs until you stop it.");
            break;

        case kCompareTag:
            plug_->setParamFromUI(trep::kCompare, c->getValue() >= 0.5f ? 1.0 : 0.0);
            setHelp(c->getValue() >= 0.5f ? "Hearing what came off the tape."
                                          : "Hearing the repair.");
            refreshCells();
            break;

        case kLearnTag:
            if (plug_->engine().learn(learnAt_ < 0.0 ? -1.0
                                       : learnAt_ * plug_->engine().takeSeconds(), 1.0)) {
                setHelp(learnAt_ < 0.0
                    ? "Floor measured from the quietest second of the take. Now REPAIR."
                    : "Floor measured from where you clicked. Now REPAIR.");
            } else {
                setHelp("Nothing to measure - record a take first.");
            }
            refreshStatus();
            break;

        case kRepairTag:
            // A side of tape is half a minute of work, so this hands the job to
            // a worker and comes straight back. The timer below reports it, and
            // pressing REPAIR again while it runs is how you call it off.
            if (plug_->engine().working()) {
                plug_->engine().cancel();
                setHelp("Stopping. The take is untouched.");
            } else if (plug_->repair()) {
                setHelp(plug_->engine().hasProfile()
                    ? "Repairing. The tape plays through untouched while it works."
                    : "Repairing, but with no floor learned the denoise will do "
                      "nothing. LEARN on a gap and REPAIR again.");
            } else {
                setHelp("Nothing to repair - record a take first.");
            }
            refreshStatus();
            break;

        case kSplitTag: {
            std::string err;
            const int n = plug_->splitToFiles(&err);
            if (n > 0) {
                char buf[200];
                snprintf(buf, sizeof buf,
                         "Wrote %d track%s into TapeGrain's samples folder. "
                         "FROM TAPE there loads the last of them.",
                         n, n == 1 ? "" : "s");
                setHelp(buf);
            } else {
                setHelp(err.empty() ? "Nothing to split." : err.c_str());
            }
            break;
        }

        case kRevertTag:
            plug_->revert();
            setHelp("Repair discarded. The tape is untouched.");
            refreshWave();
            refreshStatus();
            break;

        case kSendTag: {
            std::string err;
            const std::string path = plug_->sendToTapeGrain(&err);
            if (path.empty()) {
                setHelp(err.empty() ? "Could not write the file." : err.c_str());
            } else {
                std::string msg = "Written to TapeGrain's samples folder: " +
                                  path.substr(path.find_last_of('/') + 1);
                setHelp(msg.c_str());
            }
            break;
        }
        default: break;
    }
}

CMessageResult TapeRepairEditor::notify(CBaseObject* sender, const char* message) {
    if (message == CVSTGUITimer::kMsgTimer && plug_) {
        const trep::TapeRepair& e = plug_->engine();

        // The worker owns the take and the repair while it runs, so nothing
        // here reads them - only the two atomics it publishes. When it lets go,
        // the waveform is redrawn once, against the buffer it left behind.
        const bool busy = e.working();
        if (busy || wasWorking_) {
            refreshStatus();
            if (wasWorking_ && !busy) {
                refreshWave();
                refreshCells();
                setHelp(e.hasRepair() ? "Repaired. A/B to hear whether it helped."
                                      : "Repair stopped. The tape is as it was.");
            }
            wasWorking_ = busy;
            if (busy) return kMessageNotified;
        }

        if (wave_) {
            wave_->setPlayhead(e.playing() && e.takeSeconds() > 0
                               ? e.playSeconds() / e.takeSeconds() : -1.0);
        }

        // While recording the take keeps growing; a few redraws a second is
        // enough to watch it fill without costing anything.
        if (e.armed() && (++ticks_ % 6) == 0) { refreshWave(); refreshStatus(); }

        // ARM is no longer only pressed by hand: the host's transport can set
        // it, and the run-out can clear it. The button follows the engine.
        if (armBtn_) {
            const bool on = e.armed();
            if ((armBtn_->getValue() >= 0.5f) != on) {
                armBtn_->setValue(on ? 1.f : 0.f);
                armBtn_->invalid();
                if (!on) { refreshWave(); refreshStatus(); }
            }
        }

        // The transport can stop by itself at the end of a take.
        if (playBtn_ && !e.playing() && playBtn_->getValue() >= 0.5f) {
            playBtn_->setValue(0.f);
            playBtn_->invalid();
        }
    }
    return VSTGUIEditor::notify(sender, message);
}

void TapeRepairEditor::onMouseEntered(CView* view, CFrame*) {
    if (!view || !row_) return;
    // A cell's help is the parameter's help; everything else carries a tooltip.
    if (view == row_) return;
}
void TapeRepairEditor::onMouseExited(CView*, CFrame*) {}

} // namespace Vst
} // namespace Steinberg
