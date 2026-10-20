#include "tapegrain_editor.h"
#include "tapegrain_vst.h"

#include "vstgui/lib/cvstguitimer.h"   // CTextButton lives in cbuttons.h
#include "vstgui/lib/cgradient.h"
#include "platform.h"
#include "handoff.h"
#include "frame_open.h"
#include "ui_scale.h"

#include <filesystem>
#include <algorithm>

using namespace VSTGUI;

namespace Steinberg {
namespace Vst {

//------------------------------------------------------------------------
// One accent, everything else grey. The accent is reserved for things that
// are live or active - a value, a playing grain, the current page - so the
// eye can find state without reading anything.
static const CColor kBack    ( 12,  12,  14, 255);
static const CColor kPanel   ( 17,  17,  22, 255);
static const CColor kPanelHi ( 17,  17,  23, 255);
static const CColor kLine    ( 22,  23,  28, 255);
static const CColor kText    (214, 210, 222, 255);
static const CColor kDim     (138, 133, 152, 255);
static const CColor kFaint   ( 87,  84,  95, 255);

// The unfilled part of a knob arc. It has to be clearly visible or the
// control reads as a floating stroke rather than as a dial.
static const CColor kTrack   ( 40,  41,  49, 255);
static const CColor kAccent  (233, 162,  59, 255);   // live / active
static const CColor kAccentLo(140,  98,  38, 255);   // the same, receded
static const CColor kWave    (150, 154, 164, 255);   // audio inside the span
static const CColor kWaveDim ( 58,  60,  67, 255);   // audio outside it
static const CColor kGrain   (245, 238, 226, 255);   // a sounding grain
static const CColor kSwarmC  (150, 140, 200, 255);   // flock agents

static const double kPi = 3.14159265358979;

//------------------------------------------------------------------------
void RingKnob::draw(CDrawContext* c) {
    CRect r = getViewSize();
    const CCoord d = std::min(r.getWidth(), r.getHeight());
    CRect box(0, 0, d, d);
    box.offset(r.left + (r.getWidth() - d) * 0.5, r.top + (r.getHeight() - d) * 0.5);
    box.inset(3, 3);

    const double a0 = 135.0, sweep = 270.0;      // degrees, clockwise from SW
    const float v = getValueNormalized();

    c->setLineWidth(1.6);
    c->setDrawMode(kAntiAliasing);

    if (auto path = VSTGUI::owned(c->createGraphicsPath())) {
        path->addArc(box, a0, a0 + sweep, true);
        c->setFrameColor(kTrack);
        c->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }

    double from = a0 + sweep * origin_;
    double to   = a0 + sweep * v;
    if (to < from) std::swap(from, to);
    if (to - from > 0.7) {
        if (auto path = VSTGUI::owned(c->createGraphicsPath())) {
            path->addArc(box, from, to, true);
            c->setFrameColor(kAccent);
            c->drawGraphicsPath(path, CDrawContext::kPathStroked);
        }
    }

    // a short radial tick at the value: the arc says how much, this says where
    const double ang = (a0 + sweep * v) * kPi / 180.0;
    const CPoint cen = box.getCenter();
    const CCoord rad = box.getWidth() * 0.5;
    c->setFrameColor(kText);
    c->setLineWidth(1.4);
    c->drawLine(CPoint(cen.x + std::cos(ang) * rad * 0.45,
                       cen.y + std::sin(ang) * rad * 0.45),
                CPoint(cen.x + std::cos(ang) * rad * 0.94,
                       cen.y + std::sin(ang) * rad * 0.94));
    c->setLineWidth(1.0);
}

//------------------------------------------------------------------------
void WaveformView::draw(CDrawContext* c) {
    CRect r = getViewSize();
    c->setFillColor(kPanel);
    c->drawRect(r, kDrawFilled);
    c->setFont(kNormalFontSmall);

    if (peaks_.empty()) {
        c->setFontColor(kDim);
        c->drawString(placeholder_.empty() ? "NO SAMPLE   -   LOAD OR RECORD"
                                           : placeholder_.c_str(),
                      r, kCenterText);
        c->setFrameColor(kLine);
        c->drawRect(r, kDrawStroked);
        return;
    }

    const CCoord mid = r.top + r.getHeight() * 0.5;
    const CCoord half = r.getHeight() * 0.40;
    const CCoord W = r.getWidth();
    const float lo = std::max(0.f, pos_ - span_ * 0.5f);
    const float hi = std::min(1.f, pos_ + span_ * 0.5f);

    // faint centre rule, dotted, so quiet material still reads as audio
    c->setFrameColor(kFaint);
    for (CCoord x = r.left + 2; x < r.right; x += 5)
        c->drawLine(CPoint(x, mid), CPoint(std::min(x + 1, r.right), mid));

    // Span is shown by brightening the audio it covers rather than by tinting
    // a box over it: the sample itself carries the information.
    for (size_t i = 0; i < peaks_.size(); i++) {
        const CCoord x = r.left + 1 + (CCoord)i;
        if (x >= r.right) break;
        const float u = (float)((x - r.left) / W);
        c->setFrameColor((u >= lo && u <= hi) ? kWave : kWaveDim);
        CCoord h = peaks_[i] * half;
        if (h < 0.5) h = 0.5;
        c->drawLine(CPoint(x, mid - h), CPoint(x, mid + h));
    }

    // detected onsets: short ticks along the floor, the one being played full
    for (size_t i = 0; i < slices_.size(); i++) {
        const CCoord x = r.left + slices_[i] * W;
        if (sounding_ && (int)i == view_.activeSlice) {
            c->setFrameColor(kAccentLo);
            c->drawLine(CPoint(x, r.top + 1), CPoint(x, r.bottom - 1));
        } else {
            c->setFrameColor(kFaint);
            c->drawLine(CPoint(x, r.bottom - 6), CPoint(x, r.bottom - 1));
        }
    }

    // the edges of the span, with a handle mark so they read as draggable
    {
        const CCoord xl = r.left + lo * W, xr = r.left + hi * W;
        c->setFrameColor(kTrack);
        c->drawLine(CPoint(xl, r.top + 1), CPoint(xl, r.bottom - 1));
        c->drawLine(CPoint(xr, r.top + 1), CPoint(xr, r.bottom - 1));
        c->setFillColor(kDim);
        c->drawRect(CRect(xl - 1, mid - 10, xl + 2, mid + 10), kDrawFilled);
        c->drawRect(CRect(xr - 2, mid - 10, xr + 1, mid + 10), kDrawFilled);

        // Position: the anchor every motion mode travels out from
        const CCoord px = r.left + pos_ * W;
        c->setFrameColor(kText);
        c->drawLine(CPoint(px, r.top + 1), CPoint(px, r.bottom - 1));
    }

    if (sounding_) {
        // the flock, along the ceiling, in the modes it drives
        if (view_.motion == tg::mSwarm || view_.motion == tg::mPitchSwarm) {
            c->setFrameColor(kSwarmC);
            for (int i = 0; i < view_.numAgents; i++) {
                const CCoord x = r.left + view_.agents[i] * W;
                c->drawLine(CPoint(x, r.top + 1), CPoint(x, r.top + 7));
            }
        }

        // live grains, each riding its own window: they bloom away from the
        // centre line and sink back as they fade
        c->setFillColor(kGrain);
        for (int i = 0; i < view_.numGrains; i++) {
            const CCoord x = r.left + view_.grains[i].pos * W;
            const CCoord y = mid - view_.grains[i].env * half;
            c->drawRect(CRect(x, y, x + 2, y + 2), kDrawFilled);
        }

        // one playhead per sounding voice, the newest brightest
        for (int vi = 0; vi < tg::kMaxVoices; vi++) {
            if (view_.heads[vi] < 0.f) continue;
            c->setFrameColor(vi == view_.newestVoice ? kAccent : kAccentLo);
            const CCoord x = r.left + view_.heads[vi] * W;
            c->drawLine(CPoint(x, r.top + 1), CPoint(x, r.bottom - 1));
        }
    }

    c->setFrameColor(kLine);
    c->drawRect(r, kDrawStroked);
}

//------------------------------------------------------------------------
float WaveformView::xToNorm(CCoord x) const {
    CRect r = getViewSize();
    if (r.getWidth() <= 0) return 0.f;
    float u = (float)((x - r.left) / r.getWidth());
    return std::max(0.f, std::min(1.f, u));
}

void WaveformView::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft() || peaks_.empty() || !drag_) return;

    const float u = xToNorm(e.mousePosition.x);
    const CCoord W = getViewSize().getWidth();
    const float tol = (W > 0) ? (float)(7.0 / W) : 0.02f;   // 7 px either side
    const float lo = pos_ - span_ * 0.5f, hi = pos_ + span_ * 0.5f;

    // an edge if you grabbed near one, otherwise the whole region moves
    if (std::fabs(u - lo) < tol)      mode_ = dragLeft;
    else if (std::fabs(u - hi) < tol) mode_ = dragRight;
    else                              mode_ = dragPos;

    // keep the region under the cursor instead of jumping it to the click,
    // unless you clicked outside it, where jumping is what you meant
    grabOffset_ = (mode_ == dragPos && u > lo && u < hi) ? (pos_ - u) : 0.f;

    drag_(pos_, span_, 0);
    applyDrag(e.mousePosition.x);
    e.consumed = true;
}

void WaveformView::onMouseMoveEvent(MouseMoveEvent& e) {
    if (mode_ == dragNone || !drag_) return;
    applyDrag(e.mousePosition.x);
    e.consumed = true;
}

void WaveformView::applyDrag(CCoord x) {
    if (mode_ == dragNone || !drag_) return;

    const float u = xToNorm(x);
    float pos = pos_, span = span_;

    if (mode_ == dragPos) {
        pos = std::max(0.f, std::min(1.f, u + grabOffset_));
    } else {
        // an edge drag moves that edge only: the span follows the distance
        // from Position, doubled, since the band is centred on it
        span = std::max(0.005f, std::min(1.f, 2.f * std::fabs(u - pos_)));
    }
    drag_(pos, span, 1);
}

void WaveformView::onMouseUpEvent(MouseUpEvent& e) {
    if (mode_ == dragNone) return;
    mode_ = dragNone;
    if (drag_) drag_(pos_, span_, 2);
    e.consumed = true;
}

void WaveformView::onMouseCancelEvent(MouseCancelEvent& e) {
    if (mode_ == dragNone) return;
    mode_ = dragNone;
    if (drag_) drag_(pos_, span_, 2);
}

//------------------------------------------------------------------------
// The module's own menu hierarchy, which is the split the parameters were
// designed around.
struct Section { const char* name; int params[12]; int count; };
static const Section kSections[] = {
    // Preset lives in the always-visible bar above, next to Save and Load,
    // rather than buried on a page
    { "MAIN",   { tg::kPosition, tg::kSpan, tg::kSize, tg::kDensity,
                  tg::kGrainCount, tg::kPitch, tg::kLevel, tg::kOutput }, 8 },
    { "SHAPE",  { tg::kShape, tg::kAttack, tg::kRelease, tg::kSpray,
                  tg::kJitter, tg::kReverse, tg::kPanSpray, tg::kFocus }, 8 },
    { "MOTION", { tg::kMotion, tg::kScan, tg::kSpan, tg::kStepRate,
                  tg::kAffinity, tg::kCohesion, tg::kDrift, tg::kPitchSpread,
                  tg::kAgentCount }, 9 },
    { "PADS",   { tg::kPadMode, tg::kSensitivity, tg::kSpreadKeys,
                  tg::kVoicing, tg::kFreeze, tg::kInputMix }, 6 },
    { "SYNC",   { tg::kSync, tg::kTempo, tg::kRate }, 3 },
    { "STEMS",  { tg::kStemA, tg::kStemB, tg::kStemC, tg::kStemD, tg::kStem }, 5 },
};
static const int kNumSections = (int)(sizeof(kSections) / sizeof(kSections[0]));

// The waveform is what this instrument is about, so it gets the top of the
// window and the full width. Everything else is one row: the section tabs
// choose which eight numbers the parameter row is showing.
static const int kEdW    = 900;
static const int kMar    = 20;
static const int kWaveY  = 40;
static const int kWaveH  = 210;
static const int kStemY  = kWaveY + kWaveH + 14;   // stem slots
static const int kTranY  = kStemY + 30;            // load / freeze / record
static const int kPreY   = kTranY + 32;            // preset and sets
static const int kTabY   = kPreY + 34;             // section tabs, across
static const int kTabH   = 22;
static const int kPageY  = kTabY + kTabH + 16;     // the parameter row
static const int kPageH  = 54;
static const int kHelpY  = kPageY + kPageH + 14;
static const int kEdH    = kHelpY + 26;

static mui::Palette uiPalette() {
    return mui::Palette{kBack, CColor(27, 28, 34, 255), CColor(17, 17, 23, 255),
                        CColor(20, 21, 26, 255), kText, kDim, kFaint};
}

TapeGrainEditor::TapeGrainEditor(void* controller, TapeGrainVST* plug)
    : VSTGUIEditor(controller), plug_(plug) {
    // The size the host is told about, before it ever asks for the window.
    // Truncated, not rounded, to match what VSTGUIEditor::attached derives
    // from the zoomed frame - otherwise the two disagree by a pixel and the
    // host is asked to resize the moment it finishes opening.
    const double uiScale = mplat::editorScale();
    ViewRect vr(0, 0, (int32)(kEdW * uiScale), (int32)(kEdH * uiScale));
    setRect(vr);
}

// Labels read as small caps; the parameter names are title case in the table
// because that is what the host should show in its automation lane.
static std::string upper(const char* s) {
    std::string out;
    for (const char* p = s; p && *p; p++)
        out.push_back((char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p));
    return out;
}

CTextLabel* TapeGrainEditor::makeLabel(const CRect& r, const char* text,
                                       const CColor& col, CHoriTxtAlign align,
                                       bool mono) {
    auto* l = new CTextLabel(r, text);
    l->setBackColor(kTransparentCColor);
    l->setFrameColor(kTransparentCColor);
    l->setFontColor(col);
    l->setFont(mono ? valueFont_ : labelFont_);
    l->setHoriAlign(align);
    l->setAntialias(true);
    return l;
}

void TapeGrainEditor::styleButton(CTextButton* b) {
    b->setRoundRadius(2);
    b->setFrameWidth(1);
    b->setFrameColor(kLine);
    b->setFrameColorHighlighted(kAccent);
    b->setTextColor(kDim);
    b->setTextColorHighlighted(kAccent);
    b->setFont(labelFont_);
    // flat fills: a gradient here would be the only lit surface on the panel
    auto flat = VSTGUI::owned(CGradient::create(0., 1., kPanel, kPanel));
    auto lit  = VSTGUI::owned(CGradient::create(0., 1., kPanelHi, kPanelHi));
    b->setGradient(flat);
    b->setGradientHighlighted(lit);
}

// String128 is UTF-16; every value string this plug-in produces is ASCII.
static std::string toAscii(const char16* s) {
    std::string out;
    for (int i = 0; i < 128 && s[i]; i++)
        out.push_back((char)(s[i] < 128 ? s[i] : '?'));
    return out;
}

bool PLUGIN_API TapeGrainEditor::open(void* parent, const PlatformType& type) {
    CRect size(0, 0, kEdW, kEdH);
    frame = new CFrame(size, this);
    frame->setBackgroundColor(kBack);

    if (!mplat::openEditorFrame(frame, parent, type, plugFrame)) {
        frame = nullptr;
        return false;
    }

    // Fixed-pixel layout on a HiDPI panel is a postage stamp; scale the whole
    // frame rather than every coordinate in it. Must follow open(), which is
    // what creates the platform frame this resizes.
    frame->setZoom(mplat::editorScale());

    labelFont_ = VSTGUI::owned(new CFontDesc(mplat::kUiFont, 10));
    valueFont_ = VSTGUI::owned(new CFontDesc(mplat::kMonoFont, 11));
    bigFont_   = VSTGUI::owned(new CFontDesc(mplat::kMonoFont, 17));
    titleFont_ = VSTGUI::owned(new CFontDesc(mplat::kUiFont, 11, kBoldFace));

    tabs_.clear();
    tabMarks_.clear();
    stemBtns_.clear();
    stemSelBtns_.clear();
    bound_.clear();
    readouts_.clear();
    helpFor_.clear();

    {
        auto* t = makeLabel(CRect(kMar, 12, 300, 30), "T A P E G R A I N",
                            kText, kLeftText);
        t->setFont(titleFont_);
        t->setMouseEnabled(false);
        frame->addView(t);
    }
    voices_ = makeLabel(CRect(kEdW - 300, 12, kEdW - kMar, 30), "",
                        kDim, kRightText, true);
    voices_->setMouseEnabled(false);
    frame->addView(voices_);

    wave_ = new WaveformView(CRect(kMar, kWaveY, kEdW - kMar, kWaveY + kWaveH));
    // Dragging the waveform is the quickest way to place the cloud: the body
    // of the band moves Position, either edge widens or narrows Span.
    wave_->setDragHandler([this](float pos, float span, int phase) {
        if (!plug_) return;
        const double np = (pos - kParams[tg::kPosition].lo) /
            (kParams[tg::kPosition].hi - kParams[tg::kPosition].lo);
        const double ns = (span - kParams[tg::kSpan].lo) /
            (kParams[tg::kSpan].hi - kParams[tg::kSpan].lo);
        if (phase == 0) {
            VSTGUIEditor::beginEdit(tg::kPosition);
            VSTGUIEditor::beginEdit(tg::kSpan);
            return;
        }
        if (phase == 2) {
            VSTGUIEditor::endEdit(tg::kPosition);
            VSTGUIEditor::endEdit(tg::kSpan);
            return;
        }
        plug_->setParamFromUI(tg::kPosition, np);
        plug_->setParamFromUI(tg::kSpan, std::max(0.0, std::min(1.0, ns)));
        wave_->setRegion(pos, span);
    });
    frame->addView(wave_);

    // ---- stem slots ----
    //
    // Four buttons rather than a menu: switching source is a performance move,
    // and a menu costs a click and a read. They all write the Stem parameter,
    // so the host still sees one automatable control.
    {
        const int y = kStemY;
        auto* cap = makeLabel(CRect(kMar, y + 3, kMar + 34, y + 21), "STEM",
                              kDim, kLeftText);
        cap->setMouseEnabled(false);
        frame->addView(cap);
        attachHelp(cap, tg::kStem);

        // Two controls per slot, because they are two decisions.
        //
        // One press used to do both: turning a stem on also pointed the
        // waveform at it, and looking at a stem meant muting or unmuting it.
        // MaghrebSeq had already settled this - click a lane to select it, its
        // coloured edge to mute it - so the stems follow the same idea. The
        // light is the switch; the letter is which one you are looking at.
        for (int i = 0; i < tg::kStemSlots; i++) {
            const int x = kMar + 40 + i * 44;

            auto* on = new CTextButton(CRect(x, y, x + 16, y + 23), this,
                                       kStemOnTag + i, "", CTextButton::kOnOffStyle);
            styleButton(on);
            on->setTooltipText("Whether this stem is heard. Nothing else - it will "
                               "not move the waveform or change which slot LOAD "
                               "SAMPLE fills.");
            frame->addView(on);
            stemBtns_.push_back(on);

            auto* pick = new CTextButton(CRect(x + 20, y, x + 44, y + 23), this,
                                         kStemTag + i, kParams[tg::kStem].names[i]);
            styleButton(pick);
            pick->setTooltipText("Shows this stem on the waveform and points LOAD "
                                 "SAMPLE at it. Does not change whether it is heard. "
                                 "Levels are on the STEMS page, and each slot has its "
                                 "own output bus.");
            frame->addView(pick);
            stemSelBtns_.push_back(pick);
            attachHelp(pick, tg::kStemA + i);
        }

        auto* all = new CTextButton(CRect(kMar + 226, y, kMar + 270, y + 23),
                                    this, kStemAllTag, "ALL",
                                    CTextButton::kOnOffStyle);
        styleButton(all);
        all->setTooltipText("All four stems on. Press again to leave only the "
                            "one you last selected.");
        frame->addView(all);
        allBtn_ = all;

        // Says so when the stems are leaving on their own outputs. Without it
        // there is no way to tell from in here whether the host took them,
        // and a stem that is audible in the plug-in but routed away in the
        // DAW is a confusing five minutes.
        outs_ = makeLabel(CRect(kMar + 280, y + 3, kMar + 514, y + 21), "",
                          kAccent, kLeftText);
        outs_->setMouseEnabled(false);
        frame->addView(outs_);
    }

    // ---- transport ----
    {
        const int y = kTranY;
        auto* load = new CTextButton(CRect(kMar, y, kMar + 112, y + 23), this,
                                     kLoadTag, "LOAD SAMPLE");
        styleButton(load);
        load->setTooltipText("Load a WAV into the selected stem slot.");
        frame->addView(load);

        auto* stems = new CTextButton(CRect(kMar + 120, y, kMar + 226, y + 23),
                                      this, kLoadStemsTag, "LOAD STEMS");
        styleButton(stems);
        stems->setTooltipText(
            "Pick any one file of a set separated in Live - (Vocals), (Drums), "
            "(Bass) or (Other) - and all four are loaded into A to D at once.");
        frame->addView(stems);

        auto* sep = new CTextButton(CRect(kMar + 234, y, kMar + 330, y + 23),
                                    this, kSeparateTag, "SEPARATE");
        styleButton(sep);
        sep->setTooltipText(
            "Splits the loaded sample into four stems with Demucs and loads them "
            "into A to D. Asks for a file only if nothing is loaded. Runs in the "
            "background and takes a few minutes; press again to cancel.");
        frame->addView(sep);

        // tagged with the real Record parameter, so arming is automatable
        auto* rec = new CTextButton(CRect(kMar + 338, y, kMar + 414, y + 23),
                                    this, kRecordId, "RECORD",
                                    CTextButton::kOnOffStyle);
        styleButton(rec);
        frame->addView(rec);
        bound_.push_back(rec);
        readouts_.push_back(nullptr);       // the button is its own readout
        attachHelp(rec, kRecordId);

        auto* tape = new CTextButton(CRect(kMar + 422, y, kMar + 518, y + 23),
                                     this, kFromTapeTag, "FROM TAPE");
        styleButton(tape);
        tape->setTooltipText(
            "Loads the most recent tape sent over from TapeRepair, without "
            "going through the file dialog. The files are ordinary WAVs in this "
            "plug-in's own samples folder, so LOAD SAMPLE still finds them.");
        frame->addView(tape);

        status_ = makeLabel(CRect(kMar + 526, y + 2, kEdW - kMar, y + 21), "",
                            kDim, kRightText, true);
        status_->setMouseEnabled(false);
        frame->addView(status_);
    }

    // ---- preset bar: the five starting points, then your own sets ----
    {
        const int y = kPreY;
        auto* cap = makeLabel(CRect(kMar, y + 3, kMar + 46, y + 21), "PRESET",
                              kDim, kLeftText);
        cap->setMouseEnabled(false);
        frame->addView(cap);

        auto* pm = new COptionMenu(CRect(kMar + 52, y, kMar + 172, y + 23),
                                   this, kPresetId);
        for (int i = 0; i < kParams[kPresetId].numNames; i++)
            pm->addEntry(kParams[kPresetId].names[i]);
        pm->setMin(0.f);
        pm->setMax((float)(kParams[kPresetId].numNames - 1));
        pm->setBackColor(kPanel);
        pm->setFrameColor(kTrack);
        pm->setFontColor(kText);
        pm->setFont(labelFont_);
        pm->setHoriAlign(kLeftText);
        frame->addView(pm);
        bound_.push_back(pm);
        readouts_.push_back(nullptr);
        attachHelp(pm, kPresetId);

        auto* sp = new CTextButton(CRect(kMar + 180, y, kMar + 268, y + 23),
                                   this, kSavePreTag, "SAVE SET");
        styleButton(sp);
        sp->setTooltipText("Save every parameter and all four stem paths as a "
                           "file you can recall later.");
        frame->addView(sp);

        auto* lp = new CTextButton(CRect(kMar + 276, y, kMar + 364, y + 23),
                                   this, kLoadPreTag, "LOAD SET");
        styleButton(lp);
        lp->setTooltipText("Load one of your own saved sets, samples included.");
        frame->addView(lp);
    }

    // One hairline, separating the context above from the controls below.
    {
        auto* rule = new CTextLabel(CRect(kMar, kPageY - 12, kEdW - kMar,
                                          kPageY - 11), "");
        rule->setBackColor(kLine);
        rule->setFrameColor(kLine);
        rule->setMouseEnabled(false);
        frame->addView(rule);

    }

    // ---- section tabs, down the left, as a plain list ----
    //
    // One marker per tab, shown or hidden by colour. Moving a single marker
    // view leaves the old position painted, because setViewSize invalidates
    // where the view is going, not where it was.
    for (int i = 0; i < kNumSections; i++) {
        // the marker is the underline beneath the live tab
        auto* m = new CTextLabel(CRect(kMar + i * 96, kTabY + kTabH,
                                       kMar + i * 96 + 88, kTabY + kTabH + 2), "");
        m->setBackColor(kTransparentCColor);
        m->setFrameColor(kTransparentCColor);
        m->setMouseEnabled(false);
        frame->addView(m);
        tabMarks_.push_back(m);
    }

    for (int i = 0; i < kNumSections; i++) {
        CRect tr(kMar + i * 96, kTabY, kMar + i * 96 + 88, kTabY + kTabH);
        auto* t = new CTextButton(tr, this, kTabTag + i, kSections[i].name,
                                  CTextButton::kOnOffStyle);
        t->setRoundRadius(0);
        t->setFrameWidth(0);
        t->setFrameColor(kTransparentCColor);
        t->setFrameColorHighlighted(kTransparentCColor);
        t->setTextColor(kDim);
        t->setTextColorHighlighted(kText);
        t->setFont(labelFont_);
        auto clear = VSTGUI::owned(CGradient::create(0., 1., kTransparentCColor,
                                             kTransparentCColor));
        t->setGradient(clear);
        t->setGradientHighlighted(clear);
        frame->addView(t);
        tabs_.push_back(t);
    }

    // One row of cells, however many the section has: Motion's nine and Sync's
    // three both fit because the row divides itself. This is what replaced the
    // three-by-four grid of knobs.
    prow_ = new mui::ParamRow(CRect(kMar, kPageY, kEdW - kMar, kPageY + kPageH),
                              uiPalette(), labelFont_, bigFont_,
                              [this](int id, double d, int phase) {
                                  if (!plug_ || id < 0 || id >= kTotalParams) return;
                                  if (phase == 0) { VSTGUIEditor::beginEdit(id); return; }
                                  if (phase == 2) { VSTGUIEditor::endEdit(id); return; }
                                  double v = plug_->getParamNormalized((ParamID)id) + d;
                                  v = std::max(0.0, std::min(1.0, v));
                                  plug_->setParamFromUI(id, v);
                                  refreshValues();
                              });
    frame->addView(prow_);

    help_ = makeLabel(CRect(kMar, kHelpY, kEdW - kMar, kHelpY + 18), "",
                      kDim, kLeftText);
    help_->setMouseEnabled(false);
    frame->addView(help_);

    showSection(0);
    if (plug_) {
        plug_->setEditor(this);
        plug_->triggers().setEditorOpen(true);
        for (CControl* c : bound_) {
            double nv = plug_->getParamNormalized((ParamID)c->getTag());
            const ParamDef& d = kParams[c->getTag()];
            c->setValue(d.names ? (float)(nv * (d.numNames - 1)) : (float)nv);
        }
    }
    frame->enableTooltips(true, 600);
    frame->registerMouseObserver(this);
    setHelp(nullptr);
    refreshWaveform();
    refreshRegion();
    refreshStems();
    setIdleRate(40);      // 25 fps: fast enough to watch a grain bloom and fade
    return true;
}

void PLUGIN_API TapeGrainEditor::close() {
    if (plug_) { plug_->setEditor(nullptr); plug_->triggers().setEditorOpen(false); }
    if (frame) frame->unregisterMouseObserver(this);
    tabs_.clear();
    stemBtns_.clear();
    stemSelBtns_.clear();
    bound_.clear();
    readouts_.clear();
    helpFor_.clear();
    allBtn_ = nullptr;
    wave_ = nullptr;
    status_ = nullptr;
    outs_ = nullptr;
    voices_ = nullptr;
    help_ = nullptr;
    tabMarks_.clear();
    page_ = nullptr;
    labelFont_ = nullptr;
    valueFont_ = nullptr;
    bigFont_ = nullptr;
    prow_ = nullptr;
    titleFont_ = nullptr;
    if (frame) {
        frame->forget();
        frame = nullptr;
        mplat::closeEditorFrame();
    }
}

//------------------------------------------------------------------------
void TapeGrainEditor::setHelp(const char* text) {
    if (!help_) return;
    help_->setText(text && *text
        ? text
        : "Drag the waveform to place the cloud; drag the band's edges to widen it. "
          "Hover any control for what it does.");
}

void TapeGrainEditor::attachHelp(CView* v, int paramId) {
    if (!v || paramId < 0 || paramId >= kTotalParams) return;
    helpFor_[v] = paramId;
    v->setTooltipText(kParamHelp[paramId]);
}

void TapeGrainEditor::onMouseEntered(CView* view, CFrame*) {
    auto it = helpFor_.find(view);
    setHelp(it != helpFor_.end() ? kParamHelp[it->second] : nullptr);
}

void TapeGrainEditor::onMouseExited(CView* view, CFrame*) {
    auto it = helpFor_.find(view);
    if (it != helpFor_.end()) setHelp(nullptr);
}

//------------------------------------------------------------------------
// An arc for continuous values, a menu for the ones with named steps - an
// eight-way enum on a knob is unreadable, and Motion is the parameter that
// most changes what the instrument is.
CControl* TapeGrainEditor::addParamControl(const CRect& cell, int paramId) {
    const ParamDef& def = kParams[paramId];
    CControl* ctl = nullptr;

    if (def.names) {
        // sat on the knobs' centre line, so a row of mixed controls still
        // reads as one row
        CRect mr(cell.left + 8, cell.top + 34, cell.right - 8, cell.top + 56);
        auto* m = new COptionMenu(mr, this, paramId);
        for (int i = 0; i < def.numNames; i++) m->addEntry(def.names[i]);
        m->setMin(0.f);
        m->setMax((float)(def.numNames - 1));
        m->setBackColor(kPanel);
        m->setFrameColor(kTrack);
        m->setFontColor(kText);
        m->setFont(labelFont_);
        m->setHoriAlign(kCenterText);
        ctl = m;
    } else {
        // where zero sits, so a bipolar arc grows from the middle and Output
        // grows from unity rather than from silence
        float origin = 0.f;
        if (def.lo < 0.0 && def.hi > 0.0)
            origin = (float)((0.0 - def.lo) / (def.hi - def.lo));

        const CCoord cx = (cell.left + cell.right) * 0.5;
        CRect kr(cx - 23, cell.top + 22, cx + 23, cell.top + 68);
        ctl = new RingKnob(kr, this, paramId, origin);
    }
    page_->addView(ctl);
    return ctl;
}

void TapeGrainEditor::showSection(int index) {
    if (index < 0 || index >= kNumSections) return;
    section_ = index;

    for (int i = 0; i < (int)tabs_.size(); i++)
        tabs_[i]->setValue(i == index ? 1.f : 0.f);
    for (int i = 0; i < (int)tabMarks_.size(); i++) {
        tabMarks_[i]->setBackColor(i == index ? kAccent : kTransparentCColor);
        tabMarks_[i]->setFrameColor(i == index ? kAccent : kTransparentCColor);
        tabMarks_[i]->invalid();
    }

    refreshValues();
}

void TapeGrainEditor::beginEdit(int32_t index) {
    if (index >= kUiTagBase) return;              // chrome, not a parameter
    VSTGUIEditor::beginEdit(index);
}

void TapeGrainEditor::endEdit(int32_t index) {
    if (index >= kUiTagBase) return;
    VSTGUIEditor::endEdit(index);
}

void TapeGrainEditor::valueChanged(CControl* c) {
    if (!c || !plug_) return;
    const int32_t tag = c->getTag();

    if (tag >= kTabTag && tag < kTabTag + kNumSections) {
        showSection(tag - kTabTag);
        return;
    }

    // The light: heard or not, and nothing else.
    if (tag >= kStemOnTag && tag < kStemOnTag + tg::kStemSlots) {
        const int slot = tag - kStemOnTag;
        plug_->setParamFromUI(tg::kStemA + slot,
                              plug_->stemLevel(slot) > 0.0001f ? 0.0 : 1.0);
        refreshStems();
        refreshWaveform();
        refreshValues();
        return;
    }

    // The letter: which stem you are looking at and loading into. A kick
    // button reports twice, so only the press counts.
    if (tag >= kStemTag && tag < kStemTag + tg::kStemSlots) {
        if (c->getValue() < 0.5f) return;
        const int slot = tag - kStemTag;
        const ParamDef& d = kParams[tg::kStem];
        plug_->setParamFromUI(tg::kStem, (double)slot / (d.numNames - 1));
        refreshStems();
        refreshWaveform();
        refreshValues();
        return;
    }

    // ALL brings everything up; pressing it when everything is already up
    // leaves only the stem you last selected, which is the other half of
    // "play all four or one of them".
    if (tag == kStemAllTag) {
        bool everyOn = true;
        for (int i = 0; i < tg::kStemSlots; i++)
            if (plug_->stemLevel(i) <= 0.0001f) everyOn = false;

        const int focus = plug_->activeSlot();
        for (int i = 0; i < tg::kStemSlots; i++)
            plug_->setParamFromUI(tg::kStemA + i,
                                  everyOn ? (i == focus ? 1.0 : 0.0) : 1.0);
        refreshStems();
        refreshValues();
        return;
    }

    if (c->getValue() < 0.5f &&
        (tag == kLoadTag || tag == kLoadStemsTag || tag == kSeparateTag ||
         tag == kFromTapeTag || tag == kSavePreTag || tag == kLoadPreTag))
        return;                                 // kick buttons fire twice

    if (tag == kSeparateTag) {
        if (sep_.state() == tg::Separator::State::Running) {
            sep_.cancel();
            setHelp("Separation cancelled.");
            return;
        }
        // Demucs writes a folder per track, so they accumulate somewhere of
        // our own rather than beside the user's source file.
        const std::string out = mplat::userDataFolder("TapeGrain", "Stems");

        // Separate what is already loaded. Being asked to go and find a file
        // again when the thing to split is the sample on screen is a detour,
        // and the wrong file is easy to pick by mistake.
        if (plug_) {
            const std::string loaded = plug_->samplePath(plug_->activeSlot());
            std::error_code ec;
            if (!loaded.empty() && std::filesystem::exists(loaded, ec)) {
                sep_.start(loaded, out);
                setHelp(sep_.message().c_str());
                return;
            }
        }

        auto* sel = CNewFileSelector::create(frame, CNewFileSelector::kSelectFile);
        if (!sel) return;
        sel->setTitle("Pick a mix to separate");
        sel->addFileExtension(CFileExtension("WAVE", "wav", "audio/wav"));
        sel->run([this, out](CNewFileSelector* s) {
            if (s->getNumSelectedFiles() == 0 || !plug_) return;
            sep_.start(s->getSelectedFile(0), out);
            setHelp(sep_.message().c_str());
        });
        sel->forget();
        return;
    }

    if (tag == kFromTapeTag) {
        if (!plug_) return;
        setHelp(plug_->runTrigger(kTrigFromTape).c_str());
        refreshStems();
        return;
    }

    if (tag == kLoadTag || tag == kLoadStemsTag) {
        const bool wholeSet = (tag == kLoadStemsTag);
        auto* sel = CNewFileSelector::create(frame, CNewFileSelector::kSelectFile);
        if (!sel) return;
        sel->setTitle(wholeSet ? "Pick any one file of a separated set"
                               : "Load a sample");
        // Where TapeRepair leaves its tapes, which is also where SEPARATE
        // writes its stems - the folder worth starting in either way.
        sel->setInitialDirectory(mhand::folder().c_str());
        sel->addFileExtension(CFileExtension("WAVE", "wav", "audio/wav"));
        // loadSample calls back into refreshWaveform itself
        sel->run([this, wholeSet](CNewFileSelector* s) {
            if (s->getNumSelectedFiles() == 0 || !plug_) return;
            const char* path = s->getSelectedFile(0);

            if (wholeSet) {
                const int n = plug_->loadStemSet(path);
                if (n > 0) {
                    char buf[96];
                    snprintf(buf, sizeof buf,
                             "Loaded %d stem%s into A%s.", n, n == 1 ? "" : "s",
                             n == 4 ? " to D" : "");
                    setHelp(buf);
                    refreshStems();
                    refreshValues();
                    return;
                }
                // not part of a separated set, so treat it as a plain sample
                setHelp("Not a separated set - loaded it as a single sample.");
            }
            plug_->loadSample(path);
            refreshStems();
        });
        sel->forget();
        return;
    }

    if (tag == kSavePreTag || tag == kLoadPreTag) {
        const bool saving = (tag == kSavePreTag);
        auto* sel = CNewFileSelector::create(frame, saving
            ? CNewFileSelector::kSelectSaveFile : CNewFileSelector::kSelectFile);
        if (!sel) return;
        sel->setTitle(saving ? "Save this set" : "Load a set");
        sel->setInitialDirectory(TapeGrainVST::presetFolder().c_str());
        const CFileExtension ext("TapeGrain set", "tgset");
        sel->addFileExtension(ext);
        if (saving) {
            sel->setDefaultExtension(ext);
            sel->setDefaultSaveName("My Set.tgset");
        }
        sel->run([this, saving](CNewFileSelector* s) {
            if (s->getNumSelectedFiles() == 0 || !plug_) return;
            const char* path = s->getSelectedFile(0);
            const bool ok = saving ? plug_->savePreset(path)
                                   : plug_->loadPreset(path);
            if (!saving && ok) { refreshValues(); refreshRegion(); }
            setHelp(ok ? (saving ? "Set saved." : "Set loaded.")
                       : "Could not open that file.");
        });
        sel->forget();
        return;
    }

    if (tag < 0 || tag >= kTotalParams) return;

    // an option menu carries its index, everything else is already normalised
    double nv = c->getValue();
    if (kParams[tag].names && kParams[tag].numNames > 1)
        nv = nv / (double)(kParams[tag].numNames - 1);
    plug_->setParamFromUI(tag, nv);

    // disarming Record turns the take into the sample, so redraw it
    if (tag == kRecordId && c->getValue() < 0.5f) refreshWaveform();
    refreshValues();
}

void TapeGrainEditor::syncControl(int paramId, double normalized) {
    for (size_t i = 0; i < bound_.size(); i++) {
        CControl* c = bound_[i];
        if (!c || c->getTag() != paramId) continue;
        const ParamDef& def = kParams[paramId];
        c->setValue(def.names ? (float)(normalized * (def.numNames - 1))
                              : (float)normalized);
        c->invalid();
    }
    // the stem buttons are not in bound_, so automation has to reach them here
    if (paramId == tg::kStem) { refreshStems(); refreshWaveform(); }
    else if (paramId >= tg::kStemA && paramId <= tg::kStemD) refreshStems();
    refreshValues();
}

// The readouts come from the host's own formatting, so a value reads the same
// under the knob as it does in the automation lane.
void TapeGrainEditor::refreshValues() {
    if (!plug_) return;

    // The section decides what the row shows. An enum lands here too - its
    // value reads as the mode's name rather than a number, and dragging steps
    // through them, which is how a hardware encoder behaves anyway.
    if (prow_ && section_ >= 0 && section_ < kNumSections) {
        const Section& sec = kSections[section_];
        std::vector<mui::ParamRow::Cell> cells;
        cells.reserve(sec.count);
        for (int i = 0; i < sec.count; i++) {
            const int id = sec.params[i];
            mui::ParamRow::Cell cell;
            cell.param = id;
            cell.label = upper(kParams[id].name);
            cell.norm = (float)plug_->getParamNormalized((ParamID)id);
            cell.bar = kAccent;
            String128 str = {0};
            if (plug_->getParamStringByValue((ParamID)id, cell.norm, str) == kResultOk) {
                cell.value = toAscii(str);
                if (kParams[id].unit && kParams[id].unit[0]) {
                    cell.value += " "; cell.value += kParams[id].unit;
                }
            }
            cells.push_back(cell);
        }
        prow_->setCells(cells);
    }

    for (size_t i = 0; i < readouts_.size() && i < bound_.size(); i++) {
        if (!readouts_[i] || !bound_[i]) continue;
        const int id = bound_[i]->getTag();
        if (id < 0 || id >= kTotalParams) continue;
        String128 str = {0};
        if (plug_->getParamStringByValue((ParamID)id,
                plug_->getParamNormalized((ParamID)id), str) != kResultOk) continue;
        // toString writes the number only; the unit lives in the parameter
        // info, so the readout has to put them back together
        std::string t = toAscii(str);
        if (kParams[id].unit && kParams[id].unit[0]) {
            t += " ";
            t += kParams[id].unit;
        }
        readouts_[i]->setText(t.c_str());
    }
}

static double plainOf(TapeGrainVST* p, int id) {
    const ParamDef& d = kParams[id];
    return d.lo + p->getParamNormalized((ParamID)id) * (d.hi - d.lo);
}

// Lit means audible. An empty slot is greyed out, so you can see at a glance
// which of the four you actually loaded, and the focused one is named in the
// caption under the waveform.
void TapeGrainEditor::refreshStems() {
    if (!plug_) return;
    bool everyOn = true;
    for (int i = 0; i < (int)stemBtns_.size(); i++) {
        auto* b = dynamic_cast<CTextButton*>(stemBtns_[i]);
        if (!b) continue;
        const bool on = plug_->stemLevel(i) > 0.0001f;
        if (!on) everyOn = false;
        b->setValue(on ? 1.f : 0.f);
        // A lit frame as well as the fill: at sixteen pixels wide the fill
        // alone is not enough to read across the panel.
        b->setFrameColor(on ? kAccent : kLine);
        b->invalid();
    }

    // The letters show which slot is being looked at, and which have a sample
    // in them at all - the two things the old button could not say at once.
    const int active = plug_->activeSlot();
    for (int i = 0; i < (int)stemSelBtns_.size(); i++) {
        auto* pick = dynamic_cast<CTextButton*>(stemSelBtns_[i]);
        if (!pick) continue;
        pick->setTextColor(i == active ? kAccent
                                       : (plug_->slotFilled(i) ? kText : kFaint));
        pick->setFrameColor(i == active ? kAccent : kLine);
        pick->invalid();
    }
    if (outs_) {
        outs_->setText(plug_->splitOutputs()
                       ? "> SEPARATE OUTS: A B C D"
                       : "");
        outs_->invalid();
    }
    if (auto* a = dynamic_cast<CTextButton*>(allBtn_)) {
        a->setValue(everyOn ? 1.f : 0.f);
        a->invalid();
    }
}

void TapeGrainEditor::refreshRegion() {
    if (!wave_ || !plug_) return;
    wave_->setRegion((float)plainOf(plug_, tg::kPosition),
                     (float)plainOf(plug_, tg::kSpan));
}

void TapeGrainEditor::refreshWaveform() {
    if (!wave_ || !plug_) return;
    std::vector<float> peaks;
    std::string caption;
    plug_->buildOutline(wave_->getWidth() > 2 ? (int)wave_->getWidth() - 2 : 1,
                        peaks, caption);
    wave_->setOutline(std::move(peaks));
    wave_->setPlaceholder(caption);
    std::vector<double> sl = plug_->slices();
    wave_->setSlices(std::move(sl));
    if (status_) status_->setText(caption.c_str());
    // a load may have just filled a slot that was empty
    if (!stemBtns_.empty()) refreshStems();
}

CMessageResult TapeGrainEditor::notify(CBaseObject* sender, const char* message) {
    if (message == CVSTGUITimer::kMsgTimer && plug_) {
        // A controller's FROM TAPE, run here so the window follows it.
        plug_->triggers().drain([this](int t) {
            setHelp(plug_->runTrigger(t).c_str());
            refreshStems();
        });
        const int live = plug_->activeVoices();
        if (wave_) {
            // redraw the live layer only while something is sounding, so an
            // idle instrument with its window open costs nothing
            if (live > 0) wave_->setEngine(plug_->snapshot(), true);
            else          wave_->clearEngine();
        }

        if (voices_) {
            char buf[64];
            if (!plug_->hasSample())       snprintf(buf, sizeof buf, "NO SAMPLE");
            else if (plug_->isRecording()) snprintf(buf, sizeof buf, "RECORDING");
            else if (live == 0)            snprintf(buf, sizeof buf, "SILENT / PLAY A NOTE");
            else snprintf(buf, sizeof buf, "%d %s", live, live == 1 ? "VOICE" : "VOICES");
            voices_->setText(buf);
        }

        // cheap, and it catches host automation as well as our own edits
        refreshRegion();

        // Sensitivity re-slicing allocates, so the engine defers it to here -
        // the idle tick is the UI thread, which is where it belongs.
        plug_->refreshSlices();

        // while recording the buffer keeps growing; a couple of redraws a
        // second is enough to see it filling without costing anything
        if (plug_->isRecording() && (++ticks_ % 10) == 0) refreshWaveform();

        // Demucs, if one is running. poll() reaps the child and reads its
        // progress; the state only changes here, on the UI thread.
        if (sep_.state() == tg::Separator::State::Running) {
            const tg::Separator::State was = sep_.state();
            sep_.poll();
            if (sep_.state() == tg::Separator::State::Done) {
                const int n = plug_->loadStemSet(sep_.resultPart().c_str());
                char buf[128];
                snprintf(buf, sizeof buf, "Separated - loaded %d stem%s into A%s.",
                         n, n == 1 ? "" : "s", n == 4 ? " to D" : "");
                setHelp(n > 0 ? buf : "Separated, but the parts would not load.");
                refreshStems();
                refreshValues();
                refreshWaveform();
            } else if (sep_.state() != was) {
                setHelp(sep_.message().c_str());     // failed or cancelled
            } else if ((ticks_ % 10) == 0) {
                setHelp(sep_.message().c_str());     // progress, a few times a second
            }
        }
    }
    return VSTGUIEditor::notify(sender, message);
}

} // namespace Vst
} // namespace Steinberg
