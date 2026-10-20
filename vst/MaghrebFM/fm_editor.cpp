#include "fm_editor.h"
#include "fm_vst.h"
#include "vstgui/lib/cfileselector.h"

#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/cgradient.h"
#include "platform.h"
#include "frame_open.h"
#include "ui_scale.h"

#include <algorithm>
#include <cstdio>

using namespace VSTGUI;

namespace Steinberg {
namespace Vst {

static const CColor kBack    ( 12,  12,  14, 255);
static const CColor kPanel   ( 17,  17,  22, 255);
static const CColor kPanelHi ( 30,  30,  34, 255);
static const CColor kLine    ( 22,  23,  28, 255);
static const CColor kTrack   ( 40,  41,  49, 255);
static const CColor kText    (214, 210, 222, 255);
static const CColor kDim     (138, 133, 152, 255);
static const CColor kFaint   ( 62,  64,  70, 255);
static const CColor kAccent  (233, 162,  59, 255);
static const CColor kEditMark(120, 190, 235, 255);   // a stroke you have changed

static const double kPi = 3.14159265358979;

void RingKnob::draw(CDrawContext* c) {
    CRect r = getViewSize();
    const CCoord d = std::min(r.getWidth(), r.getHeight());
    CRect box(0, 0, d, d);
    box.offset(r.left + (r.getWidth() - d) * 0.5, r.top + (r.getHeight() - d) * 0.5);
    box.inset(2, 2);
    const double a0 = 135.0, sweep = 270.0;
    const float v = getValueNormalized();
    c->setLineWidth(1.5);
    c->setDrawMode(kAntiAliasing);
    if (auto p = VSTGUI::owned(c->createGraphicsPath())) {
        p->addArc(box, a0, a0 + sweep, true);
        c->setFrameColor(kTrack);
        c->drawGraphicsPath(p, CDrawContext::kPathStroked);
    }
    double from = a0 + sweep * origin_, to = a0 + sweep * v;
    if (to < from) std::swap(from, to);
    if (to - from > 0.7) {
        if (auto p = VSTGUI::owned(c->createGraphicsPath())) {
            p->addArc(box, from, to, true);
            c->setFrameColor(kAccent);
            c->drawGraphicsPath(p, CDrawContext::kPathStroked);
        }
    }
    const double ang = (a0 + sweep * v) * kPi / 180.0;
    const CPoint cen = box.getCenter();
    const CCoord rad = box.getWidth() * 0.5;
    c->setFrameColor(kText);
    c->drawLine(CPoint(cen.x + std::cos(ang) * rad * 0.45,
                       cen.y + std::sin(ang) * rad * 0.45),
                CPoint(cen.x + std::cos(ang) * rad * 0.92,
                       cen.y + std::sin(ang) * rad * 0.92));
    c->setLineWidth(1.0);
}

//------------------------------------------------------------------------
int PadRow::padAt(CCoord x) const {
    CRect r = getViewSize();
    const CCoord w = r.getWidth() / mfm::kNumStrokes;
    const int i = (int)((x - r.left) / w);
    return (i >= 0 && i < mfm::kNumStrokes) ? i : -1;
}

void PadRow::draw(CDrawContext* c) {
    CRect r = getViewSize();
    c->setFillColor(kPanel);
    c->drawRect(r, kDrawFilled);
    c->setFont(kNormalFontSmall);

    const CCoord w = r.getWidth() / mfm::kNumStrokes;
    for (int i = 0; i < mfm::kNumStrokes; i++) {
        CRect cell(r.left + i * w + 2, r.top + 2,
                   r.left + (i + 1) * w - 2, r.bottom - 2);
        const bool sel = (i == view_.select);
        const float lvl = std::min(1.f, view_.strokeLevel[i] * 3.f);

        // the pad lights with what it is actually putting out
        CColor fill = kPanelHi;
        if (lvl > 0.001f) {
            fill.red   = (uint8_t)(kPanelHi.red   + (kAccent.red   - kPanelHi.red)   * lvl);
            fill.green = (uint8_t)(kPanelHi.green + (kAccent.green - kPanelHi.green) * lvl);
            fill.blue  = (uint8_t)(kPanelHi.blue  + (kAccent.blue  - kPanelHi.blue)  * lvl);
        }
        c->setFillColor(fill);
        c->drawRect(cell, kDrawFilled);

        c->setFrameColor(sel ? kAccent : kLine);
        c->drawRect(cell, kDrawStroked);

        // the stroke letter, then its name and note
        char b[48];
        snprintf(b, sizeof b, "%c", mfm::kStrokeChars[i]);
        c->setFontColor(sel ? kText : kDim);
        c->drawString(b, CRect(cell.left, cell.top + 3, cell.right, cell.top + 20),
                      kCenterText);
        c->setFontColor(kDim);
        c->drawString(mfm::kStrokeNames[i],
                      CRect(cell.left + 1, cell.top + 20, cell.right - 1,
                            cell.top + 36), kCenterText);
        snprintf(b, sizeof b, "%d", mfm::kFirstNote + i);
        c->setFontColor(kFaint);
        c->drawString(b, CRect(cell.left, cell.bottom - 17, cell.right,
                               cell.bottom - 2), kCenterText);

        // a mark on anything that no longer matches the factory table
        if (edited_[i]) {
            c->setFillColor(kEditMark);
            c->drawRect(CRect(cell.right - 8, cell.top + 3,
                              cell.right - 3, cell.top + 8), kDrawFilled);
        }
    }
    c->setFrameColor(kLine);
    c->drawRect(r, kDrawStroked);
}

void PadRow::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft() || !hit_) return;
    const int i = padAt(e.mousePosition.x);
    if (i < 0) return;
    hit_(i);
    e.consumed = true;
}

//------------------------------------------------------------------------
static const int kEdW  = 764;
static const int kMar  = 14;
static const int kPadY = 34;
static const int kPadH = 62;
static const int kGlobY = kPadY + kPadH + 22;
static const int kCellW = 94;
static const int kCellH = 74;
static const int kGrpTabH = 22;
static const int kRowH    = 54;
static const int kStrokeY = kGlobY + kCellH * 2 + 40;
// The grids are gone: one tab row and one parameter row, so the window ends
// not far under the pads.
static const int kGrpTabY = kGlobY;
static const int kRowY    = kGrpTabY + kGrpTabH + 18;
static const int kHelpY   = kRowY + kRowH + 16;
static const int kEdH     = kHelpY + 26;

// two rows of globals, then two rows of the selected stroke's own numbers
static const int kGlobalRow[] = {
    mfm::kKit, mfm::kTune, mfm::kDecay, mfm::kFM, mfm::kNoise, mfm::kTone,
    mfm::kDrive, mfm::kLevel,
    mfm::kHumanize, mfm::kSpread, mfm::kVelFM, mfm::kVelTone,
    mfm::kLowpass, mfm::kHighpass, mfm::kOutput,
};
static const int kNumGlobal = (int)(sizeof(kGlobalRow) / sizeof(kGlobalRow[0]));

// Thirteen raw engine numbers in one row reads as a spec sheet. Grouped, it
// reads as a drum: what the body is, how it bends, what the skin does, and
// where it sits.
struct StrokeCell { const char* group; int param; };
static const StrokeCell kStrokeRow[] = {
    {"BODY",  mfm::kSFreq}, {nullptr, mfm::kSRatio},
              {nullptr, mfm::kSIndex}, {nullptr, mfm::kSFmDec},
    {"PITCH", mfm::kSBend}, {nullptr, mfm::kSBendDec},
    {"NOISE", mfm::kSNoise}, {nullptr, mfm::kSNoiseFreq},
              {nullptr, mfm::kSNoiseQ}, {nullptr, mfm::kSNoiseDec},
    {"VOICE", mfm::kSAmpDec}, {nullptr, mfm::kSGain}, {nullptr, mfm::kSPan},
};
static const int kNumStrokeCells = (int)(sizeof(kStrokeRow) / sizeof(kStrokeRow[0]));

// What was a caption over a column is now a tab. The kit's fifteen numbers
// split into the eight that shape the drum and the seven that place it, so no
// group is wider than eight cells.
struct Group { const char* name; int params[8]; int count; };
static const Group kGroups[] = {
    { "KIT",   { mfm::kKit, mfm::kTune, mfm::kDecay, mfm::kFM,
                 mfm::kNoise, mfm::kTone, mfm::kDrive, mfm::kLevel }, 8 },
    { "MIX",   { mfm::kHumanize, mfm::kSpread, mfm::kVelFM,
                 mfm::kVelTone, mfm::kLowpass, mfm::kHighpass,
                 mfm::kOutput }, 7 },
    { "BODY",  { mfm::kSFreq, mfm::kSRatio, mfm::kSIndex,
                 mfm::kSFmDec }, 4 },
    { "PITCH", { mfm::kSBend, mfm::kSBendDec }, 2 },
    { "NOISE", { mfm::kSNoise, mfm::kSNoiseFreq, mfm::kSNoiseQ,
                 mfm::kSNoiseDec }, 4 },
    { "VOICE", { mfm::kSAmpDec, mfm::kSGain, mfm::kSPan }, 3 },
};
static const int kNumGroups = (int)(sizeof(kGroups) / sizeof(kGroups[0]));

static mui::Palette uiPalette() {
    return mui::Palette{kBack, CColor(27, 28, 34, 255), CColor(17, 17, 23, 255),
                        CColor(20, 21, 26, 255), kText, kDim, kFaint};
}

FMEditor::FMEditor(void* controller, FMVST* plug)
    : VSTGUIEditor(controller), plug_(plug) {
    // The size the host is told about, before it ever asks for the window.
    // Truncated, not rounded, to match what VSTGUIEditor::attached derives
    // from the zoomed frame - otherwise the two disagree by a pixel and the
    // host is asked to resize the moment it finishes opening.
    const double uiScale = mplat::editorScale();
    ViewRect vr(0, 0, (int32)(kEdW * uiScale), (int32)(kEdH * uiScale));
    setRect(vr);
}

static std::string upper(const char* s) {
    std::string o;
    for (const char* p = s; p && *p; p++)
        o.push_back((char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p));
    return o;
}
static std::string toAscii(const char16* s) {
    std::string o;
    for (int i = 0; i < 128 && s[i]; i++) o.push_back((char)(s[i] < 128 ? s[i] : '?'));
    return o;
}

CTextLabel* FMEditor::makeLabel(const CRect& r, const char* text,
                                const CColor& col, CHoriTxtAlign align, bool mono) {
    auto* l = new CTextLabel(r, text);
    l->setBackColor(kTransparentCColor);
    l->setFrameColor(kTransparentCColor);
    l->setFontColor(col);
    l->setFont(mono ? valueFont_ : labelFont_);
    l->setHoriAlign(align);
    l->setAntialias(true);
    return l;
}

void FMEditor::styleButton(CTextButton* b) {
    b->setRoundRadius(2);
    b->setFrameWidth(1);
    b->setFrameColor(kLine);
    b->setFrameColorHighlighted(kAccent);
    b->setTextColor(kDim);
    b->setTextColorHighlighted(kAccent);
    b->setFont(labelFont_);
    auto flat = VSTGUI::owned(CGradient::create(0., 1., kPanel, kPanel));
    auto lit  = VSTGUI::owned(CGradient::create(0., 1., kPanelHi, kPanelHi));
    b->setGradient(flat);
    b->setGradientHighlighted(lit);
}

// name above, control in the middle, value below
void FMEditor::addCell(int x, int y, int paramId) {
    const ParamDef& d = params()[paramId];
    auto* n = makeLabel(CRect(x, y, x + kCellW, y + 16),
                        upper(d.name.c_str()).c_str(), kDim, kCenterText);
    n->setMouseEnabled(false);
    frame->addView(n);

    CControl* ctl = nullptr;
    if (d.names) {
        auto* m = new COptionMenu(CRect(x + 4, y + 26, x + kCellW - 4, y + 48),
                                  this, paramId);
        for (int i = 0; i < d.numNames; i++) m->addEntry(d.names[i]);
        m->setMin(0.f);
        m->setMax((float)(d.numNames - 1));
        m->setBackColor(kPanel);
        m->setFrameColor(kTrack);
        m->setFontColor(kText);
        m->setFont(labelFont_);
        m->setHoriAlign(kCenterText);
        frame->addView(m);
        ctl = m;
    } else {
        float origin = 0.f;
        if (d.lo < 0.0 && d.hi > 0.0) origin = (float)((0.0 - d.lo) / (d.hi - d.lo));
        const int cx = x + kCellW / 2;
        ctl = new RingKnob(CRect(cx - 20, y + 18, cx + 20, y + 58), this,
                           paramId, origin);
        frame->addView(ctl);
        auto* v = makeLabel(CRect(x, y + 58, x + kCellW, y + 72), "", kText,
                            kCenterText, true);
        frame->addView(v);
        readouts_.push_back(v);
        readoutIds_.push_back(paramId);
        attachHelp(v, paramId);
    }
    bound_.push_back(ctl);
    attachHelp(ctl, paramId);
    attachHelp(n, paramId);
}

bool PLUGIN_API FMEditor::open(void* parent, const PlatformType& type) {
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

    bound_.clear();
    readouts_.clear();
    readoutIds_.clear();
    helpFor_.clear();

    {
        auto* t = makeLabel(CRect(kMar, 8, 320, 26), "M A G H R E B   F M",
                            kText, kLeftText);
        t->setFont(titleFont_);
        t->setMouseEnabled(false);
        frame->addView(t);
    }
    status_ = makeLabel(CRect(kEdW - 380, 8, kEdW - kMar, 26), "", kDim,
                        kRightText, true);
    status_->setMouseEnabled(false);
    frame->addView(status_);

    pads_ = new PadRow(CRect(kMar, kPadY, kEdW - kMar, kPadY + kPadH));
    pads_->setHandler([this](int i) {
        if (!plug_) return;
        plug_->selectStroke(i);      // selecting and striking always want to
        plug_->audition(i);          // happen together on a drum
        refreshValues();
    });
    frame->addView(pads_);

    // ---- the groups, as tabs, and the one row they fill ----
    for (int i = 0; i < kNumGroups; i++) {
        auto* t = new CTextButton(CRect(kMar + i * 92, kGrpTabY,
                                        kMar + i * 92 + 84, kGrpTabY + kGrpTabH),
                                  this, kGroupTag + i, kGroups[i].name,
                                  CTextButton::kOnOffStyle);
        styleButton(t);
        frame->addView(t);
        groupTabs_.push_back(t);
    }

    prow_ = new mui::ParamRow(CRect(kMar, kRowY, kEdW - kMar, kRowY + kRowH),
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

    {
        strokeName_ = makeLabel(CRect(kMar, kStrokeY - 20, kMar + 420, kStrokeY - 2),
                                "STROKE", kDim, kLeftText);
        strokeName_->setMouseEnabled(false);
        frame->addView(strokeName_);
    }

    help_ = makeLabel(CRect(kMar, kHelpY, kEdW - kMar, kHelpY + 18), "",
                      kDim, kLeftText);
    help_->setMouseEnabled(false);
    frame->addView(help_);

    if (plug_) {
        plug_->setEditor(this);
        for (CControl* c : bound_) {
            const double nv = plug_->getParamNormalized((ParamID)c->getTag());
            const ParamDef& d = params()[c->getTag()];
            c->setValue(d.names ? (float)(nv * (d.numNames - 1)) : (float)nv);
        }
    }
    frame->enableTooltips(true, 600);
    frame->registerMouseObserver(this);
    setHelp(nullptr);
    showGroup(0);
    refreshValues();
    setIdleRate(33);      // 30 fps, so a pad lights on the strike
    return true;
}

void PLUGIN_API FMEditor::close() {
    if (plug_) plug_->setEditor(nullptr);
    if (frame) frame->unregisterMouseObserver(this);
    bound_.clear();
    readouts_.clear();
    readoutIds_.clear();
    helpFor_.clear();
    pads_ = nullptr;
    status_ = nullptr;
    strokeName_ = nullptr;
    help_ = nullptr;
    labelFont_ = nullptr;
    valueFont_ = nullptr;
    titleFont_ = nullptr;
    if (frame) { frame->forget(); frame = nullptr; mplat::closeEditorFrame(); }
}

void FMEditor::setHelp(const char* text) {
    if (!help_) return;
    help_->setText(text && *text ? text
        : "Click a pad to select it and hear it. The twelve controls at the "
          "bottom are that stroke's own; everything above is the whole kit.");
}

void FMEditor::attachHelp(CView* v, int paramId) {
    if (!v || paramId < 0 || paramId >= kTotalParams) return;
    helpFor_[v] = paramId;
    v->setTooltipText(params()[paramId].help.c_str());
}

void FMEditor::onMouseEntered(CView* view, CFrame*) {
    auto it = helpFor_.find(view);
    setHelp(it != helpFor_.end() ? params()[it->second].help.c_str() : nullptr);
}
void FMEditor::onMouseExited(CView* view, CFrame*) {
    if (helpFor_.find(view) != helpFor_.end()) setHelp(nullptr);
}

void FMEditor::beginEdit(int32_t i) { if (i < kUiTagBase) VSTGUIEditor::beginEdit(i); }
void FMEditor::endEdit(int32_t i)   { if (i < kUiTagBase) VSTGUIEditor::endEdit(i); }

void FMEditor::valueChanged(CControl* c) {
    if (!c || !plug_) return;
    const int32_t tag = c->getTag();

    if (tag >= kGroupTag && tag < kGroupTag + kNumGroups) {
        showGroup(tag - kGroupTag);
        return;
    }

    if (tag == kSaveKitTag || tag == kLoadKitTag) {
        if (c->getValue() < 0.5f) return;         // kick buttons fire twice
        const bool saving = (tag == kSaveKitTag);
        auto* sel = CNewFileSelector::create(frame, saving
            ? CNewFileSelector::kSelectSaveFile : CNewFileSelector::kSelectFile);
        if (!sel) return;
        sel->setTitle(saving ? "Save this kit" : "Load a kit");
        sel->setInitialDirectory(FMVST::kitFolder().c_str());
        const CFileExtension ext("Maghreb kit", "mkit");
        sel->addFileExtension(ext);
        if (saving) {
            sel->setDefaultExtension(ext);
            sel->setDefaultSaveName("My Kit.mkit");
        }
        sel->run([this, saving](CNewFileSelector* s) {
            if (s->getNumSelectedFiles() == 0 || !plug_) return;
            const char* path = s->getSelectedFile(0);
            const bool done = saving ? plug_->saveKit(path) : plug_->loadKit(path);
            setHelp(done ? (saving ? "Kit saved." : "Kit loaded.")
                         : "Could not open that file.");
            refreshValues();
        });
        sel->forget();
        return;
    }

    if (tag == kResetOneId || tag == kResetAllId) {
        if (c->getValue() < 0.5f) return;         // kick buttons fire twice
        if (tag == kResetOneId) plug_->resetStroke(plug_->selected());
        else                    plug_->resetAll();
        setHelp(tag == kResetOneId ? "Stroke restored." : "Kit restored.");
        refreshValues();
        return;
    }

    // The parameter guard lives below the buttons, not above them. Every one of
    // these tags is above kTotalParams by construction - that is what keeps
    // beginEdit from treating a button as an automation target - so a guard
    // placed first swallows the lot and the buttons quietly do nothing.
    if (tag < 0 || tag >= kTotalParams) return;

    double nv = c->getValue();
    const ParamDef& d = params()[tag];
    if (d.names && d.numNames > 1) nv = nv / (double)(d.numNames - 1);
    plug_->setParamFromUI(tag, nv);
    refreshValues();
}

void FMEditor::syncControl(int paramId, double normalized) {
    for (size_t i = 0; i < bound_.size(); i++) {
        CControl* c = bound_[i];
        if (!c || c->getTag() != paramId) continue;
        const ParamDef& d = params()[paramId];
        c->setValue(d.names ? (float)(normalized * (d.numNames - 1))
                            : (float)normalized);
        c->invalid();
    }
    refreshValues();
}

void FMEditor::showGroup(int g) {
    group_ = std::max(0, std::min(kNumGroups - 1, g));
    for (int i = 0; i < (int)groupTabs_.size(); i++) {
        groupTabs_[i]->setValue(i == group_ ? 1.f : 0.f);
        groupTabs_[i]->setTextColor(i == group_ ? kAccent : kDim);
        groupTabs_[i]->invalid();
    }
    refreshValues();
}

void FMEditor::refreshValues() {
    // The tab decides which numbers the row is showing. The first two groups
    // belong to the kit; the rest are the selected stroke's own, which is why
    // the stroke name sits above them.
    if (prow_ && plug_ && group_ >= 0 && group_ < kNumGroups) {
        const Group& gr = kGroups[group_];
        std::vector<mui::ParamRow::Cell> cells;
        cells.reserve(gr.count);
        for (int i = 0; i < gr.count; i++) {
            const int id = gr.params[i];
            mui::ParamRow::Cell cell;
            cell.param = id;
            cell.label = upper(params()[id].name.c_str());
            cell.norm = (float)plug_->getParamNormalized((ParamID)id);
            cell.bar = kAccent;
            String128 str = {0};
            if (plug_->getParamStringByValue((ParamID)id, cell.norm, str) == kResultOk) {
                cell.value = toAscii(str);
                const ParamDef& d = params()[id];
                if (!d.unit.empty()) { cell.value += " "; cell.value += d.unit; }
            }
            cells.push_back(cell);
        }
        prow_->setCells(cells);
    }

    if (!plug_) return;
    for (size_t i = 0; i < readouts_.size() && i < readoutIds_.size(); i++) {
        const int id = readoutIds_[i];
        String128 str = {0};
        if (plug_->getParamStringByValue((ParamID)id,
                plug_->getParamNormalized((ParamID)id), str) != kResultOk) continue;
        std::string t = toAscii(str);
        const ParamDef& d = params()[id];
        if (!d.unit.empty()) { t += " "; t += d.unit; }
        readouts_[i]->setText(t.c_str());
    }
    if (strokeName_) {
        const int s = plug_->selected();
        char b[96];
        snprintf(b, sizeof b, "STROKE  %c  %s   note %d%s",
                 mfm::kStrokeChars[s], upper(mfm::kStrokeNames[s]).c_str(),
                 mfm::kFirstNote + s, plug_->strokeEdited(s) ? "   EDITED" : "");
        strokeName_->setText(b);
    }
}

CMessageResult FMEditor::notify(CBaseObject* sender, const char* message) {
    if (message == CVSTGUITimer::kMsgTimer && plug_) {
        const mfm::EngineView& v = plug_->snapshot();
        if (pads_) {
            pads_->setEngine(v);
            for (int i = 0; i < mfm::kNumStrokes; i++)
                pads_->setEdited(i, plug_->strokeEdited(i));
        }
        if (status_) {
            char b[128];
            snprintf(b, sizeof b, "%d EDITED   %d %s", v.edited, v.active,
                     v.active == 1 ? "VOICE" : "VOICES");
            status_->setText(b);
        }
    }
    return VSTGUIEditor::notify(sender, message);
}

} // namespace Vst
} // namespace Steinberg
