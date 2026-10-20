#include "tubu_editor.h"
#include "tubu_vst.h"

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

static const CColor kAccent  (233, 162,  59, 255);   // a degree 12-TET has
static const CColor kNeutral (120, 190, 235, 255);   // one it does not
static const CColor kHeld    (255, 245, 225, 255);   // sounding right now
static const CColor kGrid    ( 48,  50,  57, 255);   // the twelve keys

static const double kPi = 3.14159265358979;

//------------------------------------------------------------------------
void RingKnob::draw(CDrawContext* c) {
    CRect r = getViewSize();
    const CCoord d = std::min(r.getWidth(), r.getHeight());
    CRect box(0, 0, d, d);
    box.offset(r.left + (r.getWidth() - d) * 0.5, r.top + (r.getHeight() - d) * 0.5);
    box.inset(3, 3);

    const double a0 = 135.0, sweep = 270.0;
    const float v = getValueNormalized();

    c->setLineWidth(1.6);
    c->setDrawMode(kAntiAliasing);

    if (auto path = VSTGUI::owned(c->createGraphicsPath())) {
        path->addArc(box, a0, a0 + sweep, true);
        c->setFrameColor(kTrack);
        c->drawGraphicsPath(path, CDrawContext::kPathStroked);
    }

    double from = a0 + sweep * origin_, to = a0 + sweep * v;
    if (to < from) std::swap(from, to);
    if (to - from > 0.7) {
        if (auto path = VSTGUI::owned(c->createGraphicsPath())) {
            path->addArc(box, from, to, true);
            c->setFrameColor(kAccent);
            c->drawGraphicsPath(path, CDrawContext::kPathStroked);
        }
    }

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
static const int kRulerPad = 28;      // room for the labels either side

CCoord RulerView::xForCents(double cents) const {
    CRect r = getViewSize();
    const CCoord w = r.getWidth() - kRulerPad * 2;
    return r.left + kRulerPad + (cents / 1200.0) * w;
}

int RulerView::centsForX(CCoord x) const {
    CRect r = getViewSize();
    const CCoord w = r.getWidth() - kRulerPad * 2;
    if (w <= 0) return 0;
    const double c = ((x - r.left - kRulerPad) / w) * 1200.0;
    return (int)std::max(0.0, std::min(1200.0, c + 0.5));
}

int RulerView::degreeNear(CCoord x) const {
    int best = -1;
    CCoord bestD = 14;                 // within 14 px counts as a grab
    for (int i = 0; i < tubu::kDegrees; i++) {
        const CCoord dx = std::fabs(xForCents(view_.degreeCents[i]) - x);
        if (dx < bestD) { bestD = dx; best = i; }
    }
    return best;
}

void RulerView::draw(CDrawContext* c) {
    CRect r = getViewSize();
    c->setFillColor(kPanel);
    c->drawRect(r, kDrawFilled);
    c->setFont(kNormalFontSmall);

    const CCoord top = r.top + 22, bot = r.bottom - 20;
    const CCoord base = bot;

    // the twelve keys, as a faint grid: this is the ruler the degrees are
    // being measured against
    for (int s = 0; s <= 12; s++) {
        const CCoord x = xForCents(s * 100.0);
        c->setFrameColor(s % 12 == 0 ? kTrack : kGrid);
        c->drawLine(CPoint(x, top), CPoint(x, base));
    }

    // octave ends, named
    c->setFontColor(kDim);
    c->drawString(tonic_.c_str(), CRect(r.left + 2, base + 2,
                                        r.left + kRulerPad + 20, r.bottom), kLeftText);
    c->drawString("+1200 c", CRect(r.right - 70, base + 2,
                                   r.right - 2, r.bottom), kRightText);

    // the degrees
    for (int i = 0; i < tubu::kDegrees; i++) {
        const int cents = view_.degreeCents[i];
        const CCoord x = xForCents(cents);
        const bool neutral = (cents % 100) != 0;
        const bool held = view_.heldDegree[i] > 0;

        c->setFrameColor(held ? kHeld : (neutral ? kNeutral : kAccent));
        c->setLineWidth(held ? 2.4 : 1.6);
        c->drawLine(CPoint(x, top), CPoint(x, base));
        c->setLineWidth(1.0);

        // a grab handle, so it reads as draggable
        c->setFillColor(held ? kHeld : (neutral ? kNeutral : kAccent));
        c->drawRect(CRect(x - 3, top - 7, x + 4, top), kDrawFilled);

        // the degree number above, its cents below
        char buf[24];
        snprintf(buf, sizeof buf, "%d", i + 1);
        c->setFontColor(held ? kHeld : kDim);
        c->drawString(buf, CRect(x - 12, r.top + 2, x + 12, top - 8), kCenterText);

        snprintf(buf, sizeof buf, "%d", cents);
        c->setFontColor(neutral ? kNeutral : kDim);
        c->drawString(buf, CRect(x - 22, base + 2, x + 22, r.bottom), kCenterText);
    }

    c->setFrameColor(kLine);
    c->drawRect(r, kDrawStroked);
}

void RulerView::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft() || !drag_) return;
    dragging_ = degreeNear(e.mousePosition.x);
    if (dragging_ < 0) return;
    // the tonic is the reference the others are measured from; moving it
    // would just transpose everything, which is what Tonic is for
    if (dragging_ == 0) { dragging_ = -1; return; }
    drag_(dragging_, view_.degreeCents[dragging_], 0);
    e.consumed = true;
}

void RulerView::onMouseMoveEvent(MouseMoveEvent& e) {
    if (dragging_ < 0 || !drag_) return;
    drag_(dragging_, centsForX(e.mousePosition.x), 1);
    e.consumed = true;
}

void RulerView::onMouseUpEvent(MouseUpEvent& e) {
    if (dragging_ < 0) return;
    if (drag_) drag_(dragging_, view_.degreeCents[dragging_], 2);
    dragging_ = -1;
    e.consumed = true;
}

void RulerView::onMouseCancelEvent(MouseCancelEvent& e) {
    if (dragging_ < 0) return;
    if (drag_) drag_(dragging_, view_.degreeCents[dragging_], 2);
    dragging_ = -1;
}

//------------------------------------------------------------------------
// Ten controls in one row rather than two rows of five. At 900 wide that is
// 86 pixels a cell, which is what the rest of the family uses.
static const int kEdW   = 900;
static const int kMar   = 18;
static const int kBarY  = 200;                 // tabʿ / tonic / mapping / drone
static const int kPageY = 244;
static const int kPageH = 54;
static const int kHelpY = kPageY + kPageH + 16;
static const int kEdH   = kHelpY + 26;

static mui::Palette uiPalette() {
    return mui::Palette{kBack, CColor(27, 28, 34, 255), CColor(17, 17, 23, 255),
                        CColor(20, 21, 26, 255), kText, kDim, kFaint};
}

// which controls sit on the one page, in order
static const int kPageParams[] = {
    tubu::kBrightness, tubu::kDamping, tubu::kDecay, tubu::kBow, tubu::kLevel,
    tubu::kAttack, tubu::kRelease, tubu::kDroneLevel, tubu::kEditCents, tubu::kOutput,
};
static const int kNumPageParams = (int)(sizeof(kPageParams) / sizeof(kPageParams[0]));

TubuEditor::TubuEditor(void* controller, TubuVST* plug)
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
    std::string out;
    for (const char* p = s; p && *p; p++)
        out.push_back((char)((*p >= 'a' && *p <= 'z') ? *p - 32 : *p));
    return out;
}

static std::string toAscii(const char16* s) {
    std::string out;
    for (int i = 0; i < 128 && s[i]; i++)
        out.push_back((char)(s[i] < 128 ? s[i] : '?'));
    return out;
}

CTextLabel* TubuEditor::makeLabel(const CRect& r, const char* text,
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

void TubuEditor::styleButton(CTextButton* b) {
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

bool PLUGIN_API TubuEditor::open(void* parent, const PlatformType& type) {
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
    helpFor_.clear();

    {
        auto* t = makeLabel(CRect(kMar, 12, 340, 30), "M A G H R E B   T U B \xC5\xAA",
                            kText, kLeftText);
        t->setFont(titleFont_);
        t->setMouseEnabled(false);
        frame->addView(t);
    }
    status_ = makeLabel(CRect(kEdW - 360, 12, kEdW - kMar, 30), "",
                        kDim, kRightText, true);
    status_->setMouseEnabled(false);
    frame->addView(status_);

    ruler_ = new RulerView(CRect(kMar, 36, kEdW - kMar, 168));
    ruler_->setDragHandler([this](int degree, int cents, int phase) {
        if (plug_) plug_->setDegreeFromUI(degree, cents, phase);
    });
    frame->addView(ruler_);

    // ---- the bar: what the tabʿ is and how keys reach it ----
    {
        const int y = kBarY;
        struct Item { int id; int x, w; };
        const Item items[] = {
            { tubu::kTab,     0,   170 },
            { tubu::kTonic,   178,  76 },
            { tubu::kMapMode, 262, 110 },
            { tubu::kDrone,   380, 120 },
        };
        for (const Item& it : items) {
            const ParamDef& d = kParams[it.id];
            auto* cap = makeLabel(CRect(kMar + it.x, y - 15, kMar + it.x + it.w, y),
                                  upper(d.name).c_str(), kDim, kLeftText);
            cap->setMouseEnabled(false);
            frame->addView(cap);

            CControl* ctl = nullptr;
            CRect cr(kMar + it.x, y, kMar + it.x + it.w, y + 22);
            if (d.names) {
                auto* m = new COptionMenu(cr, this, it.id);
                for (int i = 0; i < d.numNames; i++) m->addEntry(d.names[i]);
                m->setMin(0.f);
                m->setMax((float)(d.numNames - 1));
                m->setBackColor(kPanel);
                m->setFrameColor(kTrack);
                m->setFontColor(kText);
                m->setFont(labelFont_);
                m->setHoriAlign(kLeftText);
                ctl = m;
                frame->addView(ctl);
                bound_.push_back(ctl);
                readouts_.push_back(nullptr);
            } else {
                // Tonic: a knob would be silly for a note, so it is a small
                // arc with the note name under it
                auto* k = new RingKnob(CRect(cr.left, y - 2, cr.left + 34, y + 32),
                                       this, it.id, 0.f);
                frame->addView(k);
                auto* val = makeLabel(CRect(cr.left + 38, y + 2, cr.right, y + 20),
                                      "", kText, kLeftText, true);
                frame->addView(val);
                ctl = k;
                bound_.push_back(ctl);
                readouts_.push_back(val);
                attachHelp(val, it.id);
            }
            attachHelp(ctl, it.id);
            attachHelp(cap, it.id);
        }

        auto* rev = new CTextButton(CRect(kEdW - kMar - 150, y, kEdW - kMar, y + 22),
                                    this, kRevertId, "REVERT TUNING");
        styleButton(rev);
        rev->setTooltipText(kParamHelp[kRevertId]);
        frame->addView(rev);
    }

    {
        auto* rule = new CTextLabel(CRect(kMar, kPageY - 12, kEdW - kMar,
                                          kPageY - 11), "");
        rule->setBackColor(kLine);
        rule->setFrameColor(kLine);
        rule->setMouseEnabled(false);
        frame->addView(rule);
    }

    // ---- the voice ----
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

    if (plug_) {
        plug_->setEditor(this);
        for (CControl* c : bound_) {
            const double nv = plug_->getParamNormalized((ParamID)c->getTag());
            const ParamDef& d = kParams[c->getTag()];
            c->setValue(d.names ? (float)(nv * (d.numNames - 1)) : (float)nv);
        }
    }
    frame->enableTooltips(true, 600);
    frame->registerMouseObserver(this);
    setHelp(nullptr);
    refreshValues();
    setIdleRate(40);
    return true;
}

void PLUGIN_API TubuEditor::close() {
    if (plug_) plug_->setEditor(nullptr);
    if (frame) frame->unregisterMouseObserver(this);
    bound_.clear();
    readouts_.clear();
    helpFor_.clear();
    ruler_ = nullptr;
    status_ = nullptr;
    help_ = nullptr;
    labelFont_ = nullptr;
    valueFont_ = nullptr;
    titleFont_ = nullptr;
    if (frame) { frame->forget(); frame = nullptr; mplat::closeEditorFrame(); }
}

CControl* TubuEditor::addParamControl(const CRect& cell, int paramId,
                                      CViewContainer*) {
    const ParamDef& def = kParams[paramId];
    CControl* ctl = nullptr;
    if (def.names) {
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
        float origin = 0.f;
        if (def.lo < 0.0 && def.hi > 0.0)
            origin = (float)((0.0 - def.lo) / (def.hi - def.lo));
        const CCoord cx = (cell.left + cell.right) * 0.5;
        ctl = new RingKnob(CRect(cx - 23, cell.top + 22, cx + 23, cell.top + 68),
                           this, paramId, origin);
    }
    frame->addView(ctl);
    return ctl;
}

void TubuEditor::setHelp(const char* text) {
    if (!help_) return;
    help_->setText(text && *text ? text
        : "Drag a degree along the ruler to retune it. Blue degrees are the ones "
          "a keyboard has no key for.");
}

void TubuEditor::attachHelp(CView* v, int paramId) {
    if (!v || paramId < 0 || paramId >= kTotalParams) return;
    helpFor_[v] = paramId;
    v->setTooltipText(kParamHelp[paramId]);
}

void TubuEditor::onMouseEntered(CView* view, CFrame*) {
    auto it = helpFor_.find(view);
    setHelp(it != helpFor_.end() ? kParamHelp[it->second] : nullptr);
}

void TubuEditor::onMouseExited(CView* view, CFrame*) {
    if (helpFor_.find(view) != helpFor_.end()) setHelp(nullptr);
}

void TubuEditor::beginEdit(int32_t index) {
    if (index >= kUiTagBase) return;
    VSTGUIEditor::beginEdit(index);
}
void TubuEditor::endEdit(int32_t index) {
    if (index >= kUiTagBase) return;
    VSTGUIEditor::endEdit(index);
}

void TubuEditor::valueChanged(CControl* c) {
    if (!c || !plug_) return;
    const int32_t tag = c->getTag();

    if (tag == kRevertId) {
        if (c->getValue() < 0.5f) return;       // kick buttons fire twice
        plug_->revertTuning();
        refreshValues();
        setHelp("Factory tabʿ restored.");
        return;
    }

    // The parameter guard lives below the buttons, not above them. Every one of
    // these tags is above kTotalParams by construction - that is what keeps
    // beginEdit from treating a button as an automation target - so a guard
    // placed first swallows the lot and the buttons quietly do nothing.
    if (tag < 0 || tag >= kTotalParams) return;

    double nv = c->getValue();
    if (kParams[tag].names && kParams[tag].numNames > 1)
        nv = nv / (double)(kParams[tag].numNames - 1);
    plug_->setParamFromUI(tag, nv);
    refreshValues();
}

void TubuEditor::syncControl(int paramId, double normalized) {
    for (size_t i = 0; i < bound_.size(); i++) {
        CControl* c = bound_[i];
        if (!c || c->getTag() != paramId) continue;
        const ParamDef& d = kParams[paramId];
        c->setValue(d.names ? (float)(normalized * (d.numNames - 1))
                            : (float)normalized);
        c->invalid();
    }
    refreshValues();
}

void TubuEditor::refreshValues() {
    if (!plug_) return;
    if (prow_) {
        std::vector<mui::ParamRow::Cell> cells;
        cells.reserve(kNumPageParams);
        for (int i = 0; i < kNumPageParams; i++) {
            const int id = kPageParams[i];
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
        std::string t = toAscii(str);
        if (kParams[id].unit && kParams[id].unit[0]) { t += " "; t += kParams[id].unit; }
        readouts_[i]->setText(t.c_str());
    }
}

CMessageResult TubuEditor::notify(CBaseObject* sender, const char* message) {
    if (message == CVSTGUITimer::kMsgTimer && plug_) {
        const tubu::EngineView& v = plug_->snapshot();
        if (ruler_) {
            ruler_->setEngine(v);
            ruler_->setTonicName(TubuVST::noteName(plug_->tonic()));
        }
        if (status_) {
            char buf[128];
            const int live = plug_->activeVoices();
            snprintf(buf, sizeof buf, "%s%s   %d NEUTRAL   %s",
                     plug_->tabName(), v.edited ? " *" : "", v.neutral,
                     live > 0 ? "SOUNDING" : "SILENT / PLAY A NOTE");
            status_->setText(buf);
        }
    }
    return VSTGUIEditor::notify(sender, message);
}

} // namespace Vst
} // namespace Steinberg
