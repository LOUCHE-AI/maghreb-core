#include "tubudx_editor.h"
#include "tubudx_vst.h"
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
static const CColor kPanelHi ( 17,  17,  23, 255);
static const CColor kLine    ( 22,  23,  28, 255);
static const CColor kTrack   ( 40,  41,  49, 255);
static const CColor kText    (214, 210, 222, 255);
static const CColor kDim     (138, 133, 152, 255);
static const CColor kFaint   ( 87,  84,  95, 255);

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
// The tabs sit under the title and every page starts below them, so the three
// pages share one band of the window rather than each finding its own corner.
static const int kEdW    = 780;
static const int kMar    = 18;
static const int kTopY   = 12;
static const int kTabY   = 38;
static const int kTabH   = 22;
static const int kPageY  = 72;

// tuning page
static const int kRulerY  = kPageY;
static const int kRulerH  = 196;
static const int kBarY    = kRulerY + kRulerH + 26;   // tabʿ / mapping / drone
static const int kBarH    = 22;
static const int kTParamY = kBarY + kBarH + 18;
static const int kTParamH = 54;

// patch page
static const int kAlgoY  = kPageY;
static const int kOpsY   = kAlgoY + 26;
static const int kOpRowH = 56;
// Six operator colours, the same family the rest of the family uses.
static const CColor kOpColour[6] = {
    CColor(224, 163,  60, 255),   // amber
    CColor(212, 118,  74, 255),   // orange
    CColor(198,  88, 116, 255),   // rose
    CColor(150,  92, 178, 255),   // violet
    CColor( 86, 122, 196, 255),   // indigo
    CColor( 88, 176, 190, 255)    // teal
};

// The patch page's cells are patch bytes rather than parameters. Ranges are
// the DX7's own: a byte out of range makes msfa index off the end of a table
// rather than merely sounding wrong.
namespace {
struct PatchField {
    const char* label;
    int off;        // byte inside the operator block, or -1 for a voice byte
    int global;     // byte index in the patch when off < 0
    int lo, hi;
};
static const PatchField kPatchFields[2][8] = {
  { {"LEVEL", 16, -1, 0, 99}, {"COARSE", 18, -1, 0, 31}, {"FINE", 19, -1, 0, 99},
    {"DETUNE", 20, -1, 0, 14}, {"R1", 0, -1, 0, 99}, {"R2", 1, -1, 0, 99},
    {"R3", 2, -1, 0, 99}, {"R4", 3, -1, 0, 99} },
  { {"L1", 4, -1, 0, 99}, {"L2", 5, -1, 0, 99}, {"L3", 6, -1, 0, 99},
    {"L4", 7, -1, 0, 99}, {"MODE", 17, -1, 0, 1}, {"RATE SCL", 13, -1, 0, 7},
    {"FEEDBACK", -1, 135, 0, 7}, {"ALGORITHM", -1, 134, 0, 31} }
};
static int fieldByte(const PatchField& f, int op) {
    return f.off >= 0 ? (5 - op) * 21 + f.off : f.global;
}
}

static mui::Palette uiPalette() {
    return mui::Palette{kBack, CColor(27, 28, 34, 255), CColor(17, 17, 23, 255),
                        CColor(20, 21, 26, 255), kText, kDim, kFaint};
}

// cartridge page
static const int kBrowseY = kPageY;
static const int kBrowseH = 330;   // fills the page, ~18 rows a list

// the window is as tall as its tallest page, which is the six operator rows
static const int kHelpY = kOpsY + 6 * kOpRowH + 12 + 54 + 16;
static const int kEdH   = kHelpY + 26;

// The six numbers, in one row. Tonic joins them: it was a knob with a note
// name beside it, which is two controls saying one thing.
static const int kRowParams[] = {
    tdx::kTonic, tdx::kVoice, tdx::kEditCents,
    tdx::kDroneLevel, tdx::kLevel, tdx::kOutput,
};
static const int kNumRowParams = (int)(sizeof(kRowParams) / sizeof(kRowParams[0]));

TubuDXEditor::TubuDXEditor(void* controller, TubuDXVST* plug)
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

CTextLabel* TubuDXEditor::makeLabel(const CRect& r, const char* text,
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

void TubuDXEditor::styleButton(CTextButton* b) {
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

bool PLUGIN_API TubuDXEditor::open(void* parent, const PlatformType& type) {
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

    ruler_ = new RulerView(CRect(kMar, kRulerY, kEdW - kMar, kRulerY + kRulerH));
    ruler_->setDragHandler([this](int degree, int cents, int phase) {
        if (plug_) plug_->setDegreeFromUI(degree, cents, phase);
    });
    frame->addView(ruler_);
    pageViews_[0].push_back(ruler_);

    // ---- the bar: what the tabʿ is and how keys reach it ----
    {
        const int y = kBarY;
        struct Item { int id; int x, w; };
        const Item items[] = {
            { tdx::kTab,     0,   190 },
            { tdx::kMapMode, 200, 120 },
            { tdx::kDrone,   330, 130 },
        };
        for (const Item& it : items) {
            const ParamDef& d = kParams[it.id];
            auto* cap = makeLabel(CRect(kMar + it.x, y - 15, kMar + it.x + it.w, y),
                                  upper(d.name).c_str(), kDim, kLeftText);
            cap->setMouseEnabled(false);
            frame->addView(cap);
            pageViews_[0].push_back(cap);

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
                pageViews_[0].push_back(ctl);
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
        pageViews_[0].push_back(rev);
    }

    // ---- the cartridge browser ----
    //
    // Every cartridge in the folder on the left, its thirty-two voices on the
    // right, and a search box over all of them. The old arrangement - a file
    // dialog, then a numbered voice - meant 1088 patches you had to already
    // know the position of.
    {
        auto* cap = makeLabel(CRect(kMar, kBrowseY - 18, kMar + 200, kBrowseY - 2),
                              "CARTRIDGE", kDim, kLeftText);
        cap->setMouseEnabled(false);
        frame->addView(cap);
        pageViews_[2].push_back(cap);

        voiceName_ = makeLabel(CRect(kMar + 200, kBrowseY - 16, kEdW - kMar, kBrowseY),
                               "built-in patch", kText, kRightText, true);
        voiceName_->setMouseEnabled(false);
        frame->addView(voiceName_);

        mdx::BrowserPalette bp{kPanel, kPanelHi, kLine, kText, kDim, kAccent,
                               CColor(34, 35, 41, 255)};
        browser_ = new mdx::CartPanel(
            CRect(kMar, kBrowseY, kEdW - kMar, kBrowseY + kBrowseH), bp,
            labelFont_, valueFont_);
        browser_->setPick([this](const std::string& path, int voice,
                                 const std::string& label) {
            if (!plug_) return;
            if (plug_->pickCartVoice(path.c_str(), voice)) {
                refreshPatchView();
                if (voiceName_) voiceName_->setText(label.c_str());
                setHelp("Patch loaded. It plays in the tabʿ, not in twelve-tone.");
            } else {
                setHelp("That cartridge would not read.");
            }
            refreshValues();
        });
        pageViews_[2].push_back(browser_);
        // This plug-in's own pool, not one shared with the others.
        browser_->setFavouritesFile(
            mdx::favouritesFile("TubuDX"));
        browser_->rescan(TubuDXVST::cartridgeFolder());
        // A reopened Set already has a cartridge; put the lists on it rather
        // than on whatever happens to be first in the folder.
        if (plug_ && !plug_->cartPath().empty()) {
            const int v = (int)(plug_->getParamNormalized(tdx::kVoice) * 31.0 + 0.5);
            browser_->showSelection(plug_->cartPath(), v);
            if (voiceName_) voiceName_->setText(plug_->voiceName(v));
        }
        frame->addView(browser_);

        // ---- the patch page: what the loaded voice is actually made of ----
        //
        // Tubu DX plays whole DX7 voices, so until now the only thing it said
        // about one was its name. These are its six operators, its wiring and
        // its envelopes, read from the same 156 bytes the engine plays.
        algoText_ = makeLabel(CRect(kMar, kAlgoY, kEdW - kMar, kAlgoY + 18),
                              "", kDim, kLeftText, true);
        algoText_->setMouseEnabled(false);
        frame->addView(algoText_);
        pageViews_[1].push_back(algoText_);

        ops_ = new mui::OpRows(CRect(kMar, kOpsY, kEdW - kMar, kOpsY + 6 * kOpRowH),
                               uiPalette(), valueFont_, valueFont_,
                               [this](int op) { op_ = op; refreshPatchView(); });
        ops_->setSelected(op_);
        frame->addView(ops_);
        pageViews_[1].push_back(ops_);

        patchRow_ = new mui::ParamRow(
            CRect(kMar, kOpsY + 6 * kOpRowH + 12, kEdW - kMar,
                  kOpsY + 6 * kOpRowH + 12 + 54),
            uiPalette(), labelFont_, bigFont_,
            [this](int id, double d, int phase) { if (phase == 1) editPatch(id, d); });
        frame->addView(patchRow_);
        pageViews_[1].push_back(patchRow_);

        auto* pr = new CTextButton(CRect(kEdW - kMar - 90, kAlgoY - 2,
                                         kEdW - kMar, kAlgoY + 20),
                                   this, kPatchRevertId, "REVERT",
                                   CTextButton::kKickStyle);
        styleButton(pr);
        pr->setTooltipText("Throw the edits away and go back to the cartridge voice.");
        frame->addView(pr);
        pageViews_[1].push_back(pr);

        auto* pp = new CTextButton(CRect(kEdW - kMar - 140, kAlgoY - 2,
                                         kEdW - kMar - 96, kAlgoY + 20),
                                   this, kPatchPageId, "1/2", CTextButton::kKickStyle);
        styleButton(pp);
        pp->setTooltipText("The other eight values: levels, mode, feedback and algorithm.");
        frame->addView(pp);
        pageViews_[1].push_back(pp);

        // ---- the pages ----
        const char* pn[3] = {"TUNING", "PATCH", "CARTRIDGE"};
        const int32_t pt[3] = {kPageTuneId, kPagePatchId, kPageCartId};
        for (int i = 0; i < 3; i++) {
            auto* b = new CTextButton(CRect(kMar + i * 84, kTabY,
                                            kMar + i * 84 + 78, kTabY + kTabH),
                                      this, pt[i], pn[i], CTextButton::kKickStyle);
            styleButton(b);
            frame->addView(b);
            tabs_[i] = b;
        }
    }

    {
        auto* rule = new CTextLabel(CRect(kMar, kPageY - 12, kEdW - kMar,
                                          kPageY - 11), "");
        rule->setBackColor(kLine);
        rule->setFrameColor(kLine);
        rule->setMouseEnabled(false);
        frame->addView(rule);
    }

    // ---- the voice: one row of six, dragged, rather than five knobs ----
    prow_ = new mui::ParamRow(CRect(kMar, kTParamY, kEdW - kMar, kTParamY + kTParamH),
                              uiPalette(), labelFont_, bigFont_,
                              [this](int id, double d, int phase) {
                                  if (id < 0 || id >= kTotalParams) return;
                                  if (phase == 0) { VSTGUIEditor::beginEdit(id); return; }
                                  if (phase == 2) { VSTGUIEditor::endEdit(id); return; }
                                  double v = plug_->getParamNormalized((ParamID)id) + d;
                                  v = std::max(0.0, std::min(1.0, v));
                                  plug_->setParamFromUI(id, v);
                                  refreshValues();
                              });
    frame->addView(prow_);
    pageViews_[0].push_back(prow_);

    help_ = makeLabel(CRect(kMar, kHelpY, kEdW - kMar, kHelpY + 18), "",
                      kDim, kLeftText);
    help_->setMouseEnabled(false);
    frame->addView(help_);

    if (plug_) {
        plug_->setEditor(this);
        plug_->triggers().setEditorOpen(true);
        for (CControl* c : bound_) {
            const double nv = plug_->getParamNormalized((ParamID)c->getTag());
            const ParamDef& d = kParams[c->getTag()];
            c->setValue(d.names ? (float)(nv * (d.numNames - 1)) : (float)nv);
        }
    }
    frame->enableTooltips(true, 600);
    frame->registerMouseObserver(this);
    setHelp(nullptr);
    showPage(0);
    refreshValues();
    setIdleRate(40);
    return true;
}

void PLUGIN_API TubuDXEditor::close() {
    if (plug_) { plug_->setEditor(nullptr); plug_->triggers().setEditorOpen(false); }
    if (frame) frame->unregisterMouseObserver(this);
    bound_.clear();
    readouts_.clear();
    helpFor_.clear();
    ruler_ = nullptr;
    status_ = nullptr;
    voiceName_ = nullptr;
    browser_ = nullptr;
    ops_ = nullptr; algoText_ = nullptr; prow_ = nullptr; patchRow_ = nullptr;
    tabs_[0] = tabs_[1] = tabs_[2] = nullptr;
    for (int i = 0; i < 3; i++) pageViews_[i].clear();
    help_ = nullptr;
    labelFont_ = nullptr;
    valueFont_ = nullptr;
    titleFont_ = nullptr;
    if (frame) { frame->forget(); frame = nullptr; mplat::closeEditorFrame(); }
}

CControl* TubuDXEditor::addParamControl(const CRect& cell, int paramId,
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

void TubuDXEditor::setHelp(const char* text) {
    if (!help_) return;
    if (text && *text) { help_->setText(text); return; }
    // each page gets its own resting text, rather than the tuning page's
    // instructions sitting under the patch editor
    switch (page_) {
        case 1:
            help_->setText("Click an operator to select it, then drag a value. "
                           "Editing detaches the patch from the cartridge - "
                           "REVERT puts it back.");
            break;
        case 2:
            help_->setText("Click a cartridge for its thirty-two voices, or type "
                           "to search all 1088 at once.");
            break;
        default:
            help_->setText("Drag a degree along the ruler to retune it. Blue "
                           "degrees are the ones a keyboard has no key for.");
            break;
    }
}

void TubuDXEditor::attachHelp(CView* v, int paramId) {
    if (!v || paramId < 0 || paramId >= kTotalParams) return;
    helpFor_[v] = paramId;
    v->setTooltipText(kParamHelp[paramId]);
}

void TubuDXEditor::onMouseEntered(CView* view, CFrame*) {
    auto it = helpFor_.find(view);
    setHelp(it != helpFor_.end() ? kParamHelp[it->second] : nullptr);
}

void TubuDXEditor::onMouseExited(CView* view, CFrame*) {
    if (helpFor_.find(view) != helpFor_.end()) setHelp(nullptr);
}

void TubuDXEditor::beginEdit(int32_t index) {
    if (index >= kUiTagBase) return;
    VSTGUIEditor::beginEdit(index);
}
void TubuDXEditor::endEdit(int32_t index) {
    if (index >= kUiTagBase) return;
    VSTGUIEditor::endEdit(index);
}

void TubuDXEditor::showPage(int page) {
    page_ = std::max(0, std::min(2, page));
    for (int p = 0; p < 3; p++)
        for (CView* v : pageViews_[p]) if (v) v->setVisible(p == page_);
    for (int i = 0; i < 3; i++)
        if (tabs_[i]) {
            tabs_[i]->setTextColor(i == page_ ? kText : kDim);
            tabs_[i]->setFrameColor(i == page_ ? kAccent : kLine);
            tabs_[i]->invalid();
        }
    if (page_ == 1) refreshPatchView();
    setHelp(nullptr);
    if (frame) frame->invalid();
}

void TubuDXEditor::editPatch(int cellId, double delta) {
    if (!plug_ || cellId < kPatchBase) return;
    const int idx = cellId - kPatchBase;
    const int page = idx / 8, field = idx % 8;
    if (page < 0 || page > 1 || field < 0 || field > 7) return;
    const PatchField& f = kPatchFields[page][field];

    uint8_t patch[156];
    plug_->currentPatch(patch);
    const int at = fieldByte(f, op_);
    const int span = std::max(1, f.hi - f.lo);
    const int now = patch[at];
    const int want = (int)std::lround(now + delta * span * 1.6);
    if (want == now) return;
    plug_->setPatchByte(at, want, f.lo, f.hi);
    refreshPatchView();
}

void TubuDXEditor::refreshPatchView() {
    if (!plug_ || !ops_) return;
    uint8_t patch[156];
    plug_->currentPatch(patch);

    int tgt[6]; bool car[6]; int fbOp = -1;
    mdx::DxEngine::algorithmRouting(patch[134], tgt, car, fbOp);

    std::vector<mui::OpRows::Op> rows(6);
    for (int i = 0; i < 6; i++) {
        const uint8_t* o = patch + (5 - i) * 21;   // row i is OP(i+1)
        const int m = 5 - i;                        // msfa numbers them backwards
        mui::OpRows::Op& r = rows[i];
        r.colour = kOpColour[i];
        r.on = o[16] > 0;
        r.carrier = car[m];
        r.target = (tgt[m] < 0) ? -1 : (5 - tgt[m]);
        r.fb = (fbOp == m);
        for (int k = 0; k < 4; k++) { r.lev[k] = o[4 + k] / 99.f; r.rt[k] = o[k] / 99.f; }
        char b[24];
        if (o[17]) {
            const double hz = std::pow(10.0, (o[18] & 3) + o[19] / 100.0);
            if (hz >= 1000.0) snprintf(b, sizeof b, "%.1fk", hz / 1000.0);
            else              snprintf(b, sizeof b, "%.0f", hz);
        } else {
            const double ratio = (o[18] == 0 ? 0.5 : (double)o[18]) * (1.0 + o[19] / 100.0);
            snprintf(b, sizeof b, "%.2f", ratio);
        }
        r.ratio = b;
        r.haveWave = plug_->operatorWave(i, r.wave, 192);
    }
    ops_->setOps(rows);

    ops_->setSelected(op_);

    if (patchRow_) {
        std::vector<mui::ParamRow::Cell> cells;
        cells.reserve(8);
        for (int i = 0; i < 8; i++) {
            const PatchField& f = kPatchFields[patchPage_][i];
            mui::ParamRow::Cell cell;
            cell.param = kPatchBase + patchPage_ * 8 + i;
            cell.label = f.label;
            const int v = patch[fieldByte(f, op_)];
            cell.norm = (float)(v - f.lo) / (float)std::max(1, f.hi - f.lo);
            cell.bar = kOpColour[op_];
            char b[24];
            if (f.off == 20)          snprintf(b, sizeof b, "%+d", v - 7);
            else if (f.off == 17)     snprintf(b, sizeof b, "%s", v ? "FIX" : "RAT");
            else if (f.global == 134) snprintf(b, sizeof b, "%d", v + 1);
            else                      snprintf(b, sizeof b, "%d", v);
            cell.value = b;
            cells.push_back(cell);
        }
        patchRow_->setCells(cells);
    }

    if (algoText_) {
        int carriers = 0;
        for (int i = 0; i < 6; i++) if (car[i]) carriers++;
        char b[224];
        snprintf(b, sizeof b, "%s   ALG %d   %d CARRIERS   FB %d   OP %d   %s   PAGE %d",
                 plug_->cartridgeSize() ? plug_->voiceName(plug_->currentVoice())
                                        : "built-in patch",
                 patch[134] + 1, carriers, patch[135], op_ + 1,
                 plug_->patchEdited() ? "EDITED" : "AS LOADED", patchPage_ + 1);
        algoText_->setText(b);
    }
}

void TubuDXEditor::valueChanged(CControl* c) {
    if (!c || !plug_) return;
    const int32_t tag = c->getTag();


    if (tag == kPatchRevertId) {
        if (c->getValue() < 0.5f) return;
        plug_->revertPatch();
        setHelp("Back to the cartridge voice as it was loaded.");
        refreshPatchView();
        return;
    }
    if (tag == kPatchPageId) {
        if (c->getValue() < 0.5f) return;
        patchPage_ = 1 - patchPage_;
        refreshPatchView();
        return;
    }
    if (tag == kPageTuneId || tag == kPagePatchId || tag == kPageCartId) {
        if (c->getValue() < 0.5f) return;
        showPage(tag == kPageTuneId ? 0 : (tag == kPagePatchId ? 1 : 2));
        return;
    }
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

void TubuDXEditor::syncControl(int paramId, double normalized) {
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

void TubuDXEditor::refreshValues() {
    if (!plug_) return;
    if (prow_) {
        std::vector<mui::ParamRow::Cell> cells;
        cells.reserve(kNumRowParams);
        for (int i = 0; i < kNumRowParams; i++) {
            const int id = kRowParams[i];
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

CMessageResult TubuDXEditor::notify(CBaseObject* sender, const char* message) {
    if (message == CVSTGUITimer::kMsgTimer && plug_) {
        // A controller's Next Patch, run here so the window follows it.
        plug_->triggers().drain([this](int t) {
            const std::string m = plug_->runTrigger(t, browser_ ? browser_->index() : nullptr);
            if (browser_) browser_->showSelection(plug_->cartPath(), plug_->currentVoice());
            refreshPatchView();
            setHelp(m.c_str());
            refreshValues();
        });
        const tdx::EngineView& v = plug_->snapshot();
        if (ruler_) {
            ruler_->setEngine(v);
            ruler_->setTonicName(TubuDXVST::noteName(plug_->tonic()));
        }
        if (voiceName_) {
            char b[128];
            const int n = plug_->cartridgeSize();
            const int v = (int)(plug_->getParamNormalized(tdx::kVoice) * 31.0 + 0.5);
            if (n <= 0) snprintf(b, sizeof b, "built-in patch");
            else snprintf(b, sizeof b, "%02d/%02d  %s",
                          (v % n) + 1, n, plug_->voiceName(v % n));
            voiceName_->setText(b);
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
