#include "tune_editor.h"
#include "tune_vst.h"

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

//------------------------------------------------------------------- palette
static const CColor kBack   ( 12,  12,  14, 255);
static const CColor kCell   ( 17,  17,  22, 255);
static const CColor kSel    ( 17,  17,  23, 255);
static const CColor kLine   ( 22,  23,  28, 255);
static const CColor kInk    (214, 210, 222, 255);
static const CColor kLab    (138, 133, 152, 255);
static const CColor kDim    ( 87,  84,  95, 255);
static const CColor kAccent (224, 163,  60, 255);   // a degree twelve-tone has
static const CColor kNeutral(120, 190, 235, 255);   // one it does not
static const CColor kTet    ( 58,  60,  70, 255);   // the semitone grid
static const CColor kSung   (255, 245, 225, 255);   // where the voice is

static mui::Palette uiPalette() {
    return mui::Palette{kBack, CColor(27, 28, 34, 255), kSel,
                        CColor(20, 21, 26, 255), kInk, kLab, kDim};
}

static std::string toAscii(const char16* s) {
    std::string o;
    for (int i = 0; i < 128 && s[i]; i++) o.push_back((char)(s[i] < 128 ? s[i] : '?'));
    return o;
}
static std::string upper(const char* s) {
    std::string o;
    for (const char* p = s; p && *p; p++)
        o.push_back((char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p));
    return o;
}

//---------------------------------------------------------------- pitch view
static const double kSpan = 1200.0;      // one octave across the view
static const CCoord kPad = 10;

CCoord PitchView::xFor(double cents) const {
    const CRect r = getViewSize();
    const double u = std::max(0.0, std::min(1.0, cents / kSpan));
    return r.left + kPad + u * (r.getWidth() - kPad * 2);
}
int PitchView::centsFor(CCoord x) const {
    const CRect r = getViewSize();
    const double u = (x - r.left - kPad) / std::max(1.0, r.getWidth() - kPad * 2);
    return (int)std::lround(std::max(0.0, std::min(1.0, u)) * kSpan);
}
int PitchView::degreeNear(CCoord x) const {
    const int c = centsFor(x);
    int best = -1, bd = 1 << 30;
    for (int i = 0; i < tubu::kDegrees; i++) {
        const int d = std::abs(deg_[i] - c);
        if (d < bd) { bd = d; best = i; }
    }
    return (bd < 60) ? best : -1;
}

void PitchView::draw(CDrawContext* c) {
    const CRect r = getViewSize();
    c->setDrawMode(kAntiAliasing);
    c->setFillColor(kCell);
    c->drawRect(r, kDrawFilled);

    const CCoord top = r.top + 26;
    const CCoord bot = r.bottom - 30;
    const CCoord mid = (top + bot) * 0.5;

    // ---- the twelve-tone grid, underneath, as the thing being departed from
    c->setFrameColor(kTet);
    c->setFont(font_);
    for (int st = 0; st <= 12; st++) {
        const CCoord x = xFor(st * 100.0);
        c->drawLine(CPoint(x, bot), CPoint(x, bot + 8));
    }

    // ---- the degrees of the tabʿ ----
    for (int i = 0; i < tubu::kDegrees; i++) {
        const bool neutral = (deg_[i] % 100) != 0;
        const CCoord x = xFor(deg_[i]);
        c->setFrameColor(neutral ? kNeutral : kAccent);
        c->setLineWidth(neutral ? 2.0 : 1.4);
        c->drawLine(CPoint(x, top), CPoint(x, bot));
        c->setLineWidth(1.0);

        char b[16]; snprintf(b, sizeof b, "%d", deg_[i]);
        c->setFontColor(neutral ? kNeutral : kDim);
        c->drawString(b, CRect(x - 26, bot + 10, x + 26, bot + 26), kCenterText);
    }

    // ---- where the voice is, and where it is being taken ----
    if (view_.voiced && view_.inHz > 0.0) {
        // cents above the tonic, folded into the octave the ruler shows
        const double tonicHz = view_.targetHz > 0.0 && view_.degree >= 0
            ? view_.targetHz * std::pow(2.0, -deg_[view_.degree] / 1200.0)
            : view_.inHz;
        double sung = 1200.0 * std::log2(view_.inHz / tonicHz);
        while (sung < 0.0) sung += 1200.0;
        while (sung >= 1200.0) sung -= 1200.0;

        const CCoord xs = xFor(sung);
        c->setFrameColor(kSung);
        c->setLineWidth(2.0);
        c->drawLine(CPoint(xs, top - 8), CPoint(xs, bot));
        c->setLineWidth(1.0);

        if (view_.degree >= 0) {
            const CCoord xt = xFor(deg_[view_.degree]);
            // the pull: a line from the voice to the degree it is going to
            c->setFrameColor(kAccent);
            c->drawLine(CPoint(xs, mid), CPoint(xt, mid));
            const double dir = (xt >= xs) ? -1.0 : 1.0;
            c->drawLine(CPoint(xt, mid), CPoint(xt + dir * 6, mid - 4));
            c->drawLine(CPoint(xt, mid), CPoint(xt + dir * 6, mid + 4));
        }
    }

    c->setFontColor(kDim);
    c->drawString(tonic_.c_str(), CRect(r.left + 6, r.top + 4, r.left + 80, r.top + 20),
                  kLeftText);
    c->drawString("+1200 c", CRect(r.right - 80, r.top + 4, r.right - 6, r.top + 20),
                  kRightText);

    c->setFrameColor(kLine);
    c->drawRect(r, kDrawStroked);
}

void PitchView::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft()) return;
    const int d = degreeNear(e.mousePosition.x);
    if (d < 0) return;
    dragging_ = d;
    if (drag_) drag_(d, centsFor(e.mousePosition.x), 0);
    e.consumed = true;
}
void PitchView::onMouseMoveEvent(MouseMoveEvent& e) {
    if (dragging_ < 0 || !drag_) return;
    drag_(dragging_, centsFor(e.mousePosition.x), 1);
    e.consumed = true;
}
void PitchView::onMouseUpEvent(MouseUpEvent& e) {
    if (dragging_ < 0) return;
    if (drag_) drag_(dragging_, centsFor(e.mousePosition.x), 2);
    dragging_ = -1;
    e.consumed = true;
}

//--------------------------------------------------------------------- layout
static const int kEdW    = 820;
static const int kMar    = 18;
static const int kTopY   = 14;
static const int kBarY   = 44;             // tabʿ, bypass, revert
static const int kBarH   = 22;
static const int kPitchY = kBarY + kBarH + 16;
static const int kPitchH = 168;
static const int kRowY   = kPitchY + kPitchH + 18;
static const int kRowH   = 54;
static const int kHelpY  = kRowY + kRowH + 14;
static const int kEdH    = kHelpY + 24;

TuneEditor::TuneEditor(void* controller, TuneVST* plug)
    : VSTGUIEditor(controller), plug_(plug) {
    // The size the host is told about, before it ever asks for the window.
    // Truncated, not rounded, to match what VSTGUIEditor::attached derives
    // from the zoomed frame - otherwise the two disagree by a pixel and the
    // host is asked to resize the moment it finishes opening.
    const double uiScale = mplat::editorScale();
    ViewRect vr(0, 0, (int32)(kEdW * uiScale), (int32)(kEdH * uiScale));
    setRect(vr);
}

CTextLabel* TuneEditor::makeLabel(const CRect& r, const char* text, const CColor& col,
                                  CHoriTxtAlign align, bool mono) {
    auto* l = new CTextLabel(r, text);
    l->setBackColor(kTransparentCColor);
    l->setFrameColor(kTransparentCColor);
    l->setFontColor(col);
    l->setFont(mono ? valueFont_ : labelFont_);
    l->setHoriAlign(align);
    l->setAntialias(true);
    return l;
}

void TuneEditor::styleButton(CTextButton* b) {
    b->setRoundRadius(2);
    b->setFrameWidth(1);
    b->setFrameColor(kLine);
    b->setFrameColorHighlighted(kAccent);
    b->setTextColor(kLab);
    b->setTextColorHighlighted(kAccent);
    b->setFont(labelFont_);
    auto flat = VSTGUI::owned(CGradient::create(0., 1., kCell, kCell));
    auto lit  = VSTGUI::owned(CGradient::create(0., 1., kSel, kSel));
    b->setGradient(flat);
    b->setGradientHighlighted(lit);
}

bool PLUGIN_API TuneEditor::open(void* parent, const PlatformType& type) {
    CRect size(0, 0, kEdW, kEdH);
    frame = new CFrame(size, this);
    frame->setBackgroundColor(kBack);
    if (!mplat::openEditorFrame(frame, parent, type, plugFrame)) { frame = nullptr; return false; }

    // Fixed-pixel layout on a HiDPI panel is a postage stamp; scale the whole
    // frame rather than every coordinate in it. Must follow open(), which is
    // what creates the platform frame this resizes.
    frame->setZoom(mplat::editorScale());

    labelFont_ = VSTGUI::owned(new CFontDesc(mplat::kUiFont, 9));
    valueFont_ = VSTGUI::owned(new CFontDesc(mplat::kMonoFont, 11));
    bigFont_   = VSTGUI::owned(new CFontDesc(mplat::kMonoFont, 17));
    titleFont_ = VSTGUI::owned(new CFontDesc(mplat::kMonoFont, 11));

    bound_.clear();
    helpFor_.clear();

    {
        auto* t = makeLabel(CRect(kMar, kTopY, kMar + 240, kTopY + 18),
                            "M A G H R E B   T U N E", kInk, kLeftText, true);
        t->setFont(titleFont_);
        t->setMouseEnabled(false);
        frame->addView(t);

        status_ = makeLabel(CRect(kEdW - kMar - 460, kTopY, kEdW - kMar, kTopY + 18),
                            "", kDim, kRightText, true);
        status_->setMouseEnabled(false);
        frame->addView(status_);
    }

    // ---- the style, then the tabʿ, then the two switches ----
    //
    // Style comes first because it is what you reach for: it sets the kind of
    // correction and loads a sensible ṭabʿ, and then the ṭabʿ is the one thing
    // you change per song.
    {
        auto* scap = makeLabel(CRect(kMar, kBarY, kMar + 42, kBarY + kBarH), "STYLE",
                               kDim, kLeftText);
        scap->setMouseEnabled(false);
        frame->addView(scap);

        styleMenu_ = new COptionMenu(CRect(kMar + 46, kBarY, kMar + 196, kBarY + kBarH),
                                     this, kStyleId);
        for (int i = 0; i < mtune::kNumPresets; i++)
            styleMenu_->addEntry(mtune::kPresets[i].name);
        styleMenu_->setMin(0.f);
        styleMenu_->setMax((float)(mtune::kNumPresets - 1));
        styleMenu_->setBackColor(kCell);
        styleMenu_->setFrameColor(kLine);
        styleMenu_->setFontColor(kInk);
        styleMenu_->setFont(valueFont_);
        styleMenu_->setHoriAlign(kLeftText);
        styleMenu_->setTooltipText("A starting point. Sets the kind of correction "
                                   "and loads a sensible tabʿ; change the tabʿ per song.");
        frame->addView(styleMenu_);
    }

    {
        auto* cap = makeLabel(CRect(kMar + 210, kBarY, kMar + 244, kBarY + kBarH), "TAB",
                              kDim, kLeftText);
        cap->setMouseEnabled(false);
        frame->addView(cap);

        const ParamDef& d = params()[mtune::kTab];
        tabMenu_ = new COptionMenu(CRect(kMar + 248, kBarY, kMar + 418, kBarY + kBarH),
                                   this, mtune::kTab);
        for (int i = 0; i < d.numNames; i++) tabMenu_->addEntry(d.names[i]);
        tabMenu_->setMin(0.f);
        tabMenu_->setMax((float)(d.numNames - 1));
        tabMenu_->setBackColor(kCell);
        tabMenu_->setFrameColor(kLine);
        tabMenu_->setFontColor(kInk);
        tabMenu_->setFont(valueFont_);
        tabMenu_->setHoriAlign(kLeftText);
        frame->addView(tabMenu_);
        bound_.push_back(tabMenu_);
        attachHelp(tabMenu_, mtune::kTab);

        bypassBtn_ = new CTextButton(CRect(kMar + 430, kBarY, kMar + 500, kBarY + kBarH),
                                     this, mtune::kBypass, "BYPASS",
                                     CTextButton::kOnOffStyle);
        styleButton(bypassBtn_);
        frame->addView(bypassBtn_);
        bound_.push_back(bypassBtn_);
        attachHelp(bypassBtn_, mtune::kBypass);

        auto* liveBtn = new CTextButton(CRect(kMar + 510, kBarY, kMar + 570,
                                              kBarY + kBarH),
                                        this, mtune::kLive, "LIVE",
                                        CTextButton::kOnOffStyle);
        styleButton(liveBtn);
        frame->addView(liveBtn);
        bound_.push_back(liveBtn);
        attachHelp(liveBtn, mtune::kLive);

        auto* rev = new CTextButton(CRect(kEdW - kMar - 130, kBarY, kEdW - kMar,
                                          kBarY + kBarH),
                                    this, kRevertId, "REVERT TUNING",
                                    CTextButton::kKickStyle);
        styleButton(rev);
        rev->setTooltipText("Put the tabʿ's written degrees back.");
        frame->addView(rev);
    }

    pitch_ = new PitchView(CRect(kMar, kPitchY, kEdW - kMar, kPitchY + kPitchH),
                           valueFont_,
                           [this](int degree, int cents, int phase) {
                               if (!plug_) return;
                               if (phase == 0) setHelp("Dragging a degree.");
                               plug_->setDegreeFromUI(degree, cents);
                               refreshValues();
                           });
    frame->addView(pitch_);

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

    help_ = makeLabel(CRect(kMar, kHelpY, kEdW - kMar, kHelpY + 18), "", kDim, kLeftText);
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
    refreshValues();
    setIdleRate(30);
    return true;
}

void PLUGIN_API TuneEditor::close() {
    if (plug_) plug_->setEditor(nullptr);
    if (frame) frame->unregisterMouseObserver(this);
    bound_.clear();
    helpFor_.clear();
    pitch_ = nullptr; prow_ = nullptr; status_ = nullptr; help_ = nullptr;
    tabMenu_ = nullptr; styleMenu_ = nullptr; bypassBtn_ = nullptr;
    labelFont_ = nullptr; valueFont_ = nullptr; bigFont_ = nullptr; titleFont_ = nullptr;
    if (frame) { frame->forget(); frame = nullptr; mplat::closeEditorFrame(); }
}

void TuneEditor::setHelp(const char* text) {
    if (!help_) return;
    help_->setText(text && *text ? text
        : "Blue degrees are the ones twelve-tone cannot reach. Drag one to retune it.");
}

void TuneEditor::attachHelp(CView* v, int paramId) {
    if (!v || paramId < 0 || paramId >= kTotalParams) return;
    helpFor_[v] = paramId;
    v->setTooltipText(params()[paramId].help.c_str());
}

void TuneEditor::onMouseEntered(CView* view, CFrame*) {
    auto it = helpFor_.find(view);
    setHelp(it != helpFor_.end() ? params()[it->second].help.c_str() : nullptr);
}
void TuneEditor::onMouseExited(CView* view, CFrame*) {
    if (helpFor_.find(view) != helpFor_.end()) setHelp(nullptr);
}

void TuneEditor::beginEdit(int32_t i) { if (i < kUiTagBase) VSTGUIEditor::beginEdit(i); }
void TuneEditor::endEdit(int32_t i)   { if (i < kUiTagBase) VSTGUIEditor::endEdit(i); }

void TuneEditor::valueChanged(CControl* c) {
    if (!c || !plug_) return;
    const int32_t tag = c->getTag();

    if (tag == kStyleId) {
        const int i = (int)std::lround(c->getValue());
        plug_->applyPreset(i);
        setHelp(mtune::kPresets[std::max(0, std::min(mtune::kNumPresets - 1, i))].note);
        refreshValues();
        return;
    }
    if (tag == kRevertId) {
        if (c->getValue() < 0.5f) return;
        plug_->revertTuning();
        setHelp("The written degrees are back.");
        refreshValues();
        return;
    }

    if (tag < 0 || tag >= kTotalParams) return;
    double nv = c->getValue();
    const ParamDef& d = params()[tag];
    if (d.names && d.numNames > 1) nv = nv / (double)(d.numNames - 1);
    plug_->setParamFromUI(tag, nv);
    refreshValues();
}

void TuneEditor::syncControl(int paramId, double normalized) {
    for (CControl* c : bound_) {
        if (!c || c->getTag() != paramId) continue;
        const ParamDef& d = params()[paramId];
        c->setValue(d.names ? (float)(normalized * (d.numNames - 1)) : (float)normalized);
        c->invalid();
    }
    refreshValues();
}

void TuneEditor::refreshValues() {
    if (!plug_) return;

    static const int kRow[] = {
        mtune::kTonic, mtune::kTonicFine, mtune::kAmount, mtune::kRetune,
        mtune::kDeadzone, mtune::kMix, mtune::kOutput
    };
    const int n = (int)(sizeof(kRow) / sizeof(kRow[0]));

    if (prow_) {
        std::vector<mui::ParamRow::Cell> cells;
        cells.reserve(n);
        for (int i = 0; i < n; i++) {
            const int id = kRow[i];
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

    if (styleMenu_) {
        styleMenu_->setValue((float)plug_->currentPreset());
        styleMenu_->invalid();
    }

    if (pitch_) {
        int deg[tubu::kDegrees];
        for (int i = 0; i < tubu::kDegrees; i++) deg[i] = plug_->degreeCents(i);
        pitch_->setDegrees(deg);
    }

    for (CControl* c : bound_) {
        if (!c) continue;
        const int id = c->getTag();
        if (id < 0 || id >= kTotalParams) continue;
        const ParamDef& d = params()[id];
        const double nv = plug_->getParamNormalized((ParamID)id);
        c->setValue(d.names ? (float)(nv * (d.numNames - 1)) : (float)nv);
        c->invalid();
    }
}

CMessageResult TuneEditor::notify(CBaseObject* sender, const char* message) {
    if (message == CVSTGUITimer::kMsgTimer && plug_) {
        const mtune::EngineView& v = plug_->snapshot();
        if (pitch_) pitch_->setEngine(v);
        if (status_) {
            char buf[240];
            if (v.voiced && v.inHz > 0.0)
                snprintf(buf, sizeof buf,
                         "%s  %d NEUTRAL  /  %.1f Hz  %+.0f c OFF  MOVING %+.0f c%s",
                         plug_->tabName(), plug_->neutralCount(),
                         v.inHz, v.offCents, v.moveCents,
                         plug_->tuningEdited() ? "  /  EDITED" : "");
            else
                snprintf(buf, sizeof buf, "%s  %d NEUTRAL  /  SILENT%s",
                         plug_->tabName(), plug_->neutralCount(),
                         plug_->tuningEdited() ? "  /  EDITED" : "");
            status_->setText(buf);
        }
    }
    return VSTGUIEditor::notify(sender, message);
}

} // namespace Vst
} // namespace Steinberg
