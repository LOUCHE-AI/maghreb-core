#include "seq_editor.h"
#include "seq_vst.h"

#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/cgradient.h"
#include "vstgui/lib/cfileselector.h"
#include "platform.h"
#include "frame_open.h"
#include "ui_scale.h"

#include <algorithm>
#include <cstdio>

using namespace VSTGUI;

namespace Steinberg {
namespace Vst {

//------------------------------------------------------------------- palette
static const CColor kBg    ( 12,  12,  14, 255);
static const CColor kRest  ( 25,  26,  31, 255);   // a step that is not struck
static const CColor kBeat  ( 36,  37,  46, 255);   // every fourth one, lifted
static const CColor kInk   (214, 210, 222, 255);
static const CColor kLab   (138, 133, 152, 255);
static const CColor kDim   ( 87,  84,  95, 255);
static const CColor kRule  ( 22,  23,  28, 255);
static const CColor kSelBg ( 17,  17,  23, 255);
static const CColor kWhite (242, 238, 250, 255);
static const CColor kCellBg( 17,  17,  22, 255);
static const CColor kCurBg ( 26,  22,  32, 255);

// Six operator colours from the same family as the four lanes, so the window
// has one palette rather than two.
static const CColor kOpColour[6] = {
    CColor(224, 163,  60, 255),   // OP1 body carrier
    CColor(208, 106,  87, 255),   // OP2 body modulator
    CColor( 70,  68,  78, 255),   // OP3 silent in the derivation
    CColor( 70,  68,  78, 255),   // OP4 silent
    CColor(158,  91, 166, 255),   // OP5 noise carrier
    CColor( 78, 127, 184, 255)    // OP6 noise modulator, the one with feedback
};

const CColor kLaneColour[mseq::kLanes] = {
    CColor(224, 163,  60, 255),   // amber
    CColor(208, 106,  87, 255),   // coral
    CColor(158,  91, 166, 255),   // violet
    CColor( 78, 127, 184, 255)    // indigo
};

// A silenced lane has to stay readable without competing, so it is the same
// drawing at a third of the brightness rather than a different one.
static mui::Palette uiPalette() {
    return mui::Palette{kBg, CColor(27, 28, 34, 255), kSelBg, CColor(20, 21, 26, 255),
                        kInk, kLab, kDim};
}

static CColor dimmed(const CColor& c, float f) {
    return CColor((uint8_t)(c.red * f), (uint8_t)(c.green * f),
                  (uint8_t)(c.blue * f), c.alpha);
}

static std::string toAscii(const char16* s) {
    std::string o;
    for (int i = 0; i < 128 && s[i]; i++) o.push_back((char)(s[i] < 128 ? s[i] : '?'));
    return o;
}

//----------------------------------------------------------------- lane grid
static const CCoord kEdgeW = 4, kNumW = 14, kNameW = 132, kMetW = 40, kTrigGap = 3;
static const CCoord kTrigX = kEdgeW + 8 + kNumW + kNameW + kMetW;

void LaneGrid::setLane(int lane, const char* steps, const char* name, const char* meter) {
    if (lane < 0 || lane >= mseq::kLanes) return;
    if (steps_[lane] == steps && names_[lane] == name) return;
    steps_[lane] = steps; names_[lane] = name; meters_[lane] = meter;
    invalid();
}

void LaneGrid::draw(CDrawContext* c) {
    const CRect r = getViewSize();
    const CCoord rowH = r.getHeight() / mseq::kLanes;
    c->setDrawMode(kAntiAliasing);

    for (int lane = 0; lane < mseq::kLanes; lane++) {
        const CCoord top = r.top + lane * rowH;
        const bool playing = view_.lanes[lane].playing;
        const float f = playing ? 1.f : 0.34f;
        const CColor col = dimmed(kLaneColour[lane], f);

        if (lane == sel_) {
            c->setFillColor(kSelBg);
            c->drawRect(CRect(r.left, top, r.right, top + rowH), kDrawFilled);
        }

        // the coloured edge is both the lane's identity and its mute target
        c->setFillColor(col);
        c->drawRect(CRect(r.left, top + rowH * 0.5 - 13, r.left + kEdgeW,
                          top + rowH * 0.5 + 13), kDrawFilled);

        c->setFont(font_);
        char n[8]; snprintf(n, sizeof n, "%d", lane + 1);
        c->setFontColor(dimmed(kDim, f));
        c->drawString(n, CRect(r.left + kEdgeW + 6, top,
                               r.left + kEdgeW + 6 + kNumW, top + rowH), kLeftText);
        c->setFontColor(dimmed(kInk, f));
        c->drawString(names_[lane].c_str(),
                      CRect(r.left + kEdgeW + 8 + kNumW, top,
                            r.left + kEdgeW + 8 + kNumW + kNameW, top + rowH), kLeftText);
        c->setFontColor(dimmed(kDim, f));
        c->drawString(meters_[lane].c_str(),
                      CRect(r.left + kEdgeW + 8 + kNumW + kNameW, top,
                            r.left + kTrigX, top + rowH), kLeftText);

        const std::string& pat = steps_[lane];
        const int steps = (int)pat.size();
        if (steps <= 0) continue;

        // Each lane is drawn at its own length across the same width, so two
        // rows of different cycle lengths line up at their starts and part
        // company after - which is the thing worth seeing.
        const CCoord x0 = r.left + kTrigX;
        const CCoord W = r.right - x0;
        const CCoord cw = (W - (steps - 1) * kTrigGap) / steps;
        const CCoord ty = top + rowH * 0.5 - 10.5;

        for (int s = 0; s < steps; s++) {
            const CCoord x = x0 + s * (cw + kTrigGap);
            const CRect cell(x, ty, x + cw, ty + 21);
            const bool hit = pat[s] != '.';
            c->setFillColor(hit ? col : (s % 4 == 0 ? kBeat : kRest));
            c->drawRect(cell, kDrawFilled);
            if (s == view_.lanes[lane].step && view_.running) {
                c->setFrameColor(kWhite);
                c->setLineWidth(1.5);
                c->drawRect(cell, kDrawStroked);
                c->setLineWidth(1.0);
            }
        }
    }
}

void LaneGrid::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft()) return;
    const CRect r = getViewSize();
    const CCoord rowH = r.getHeight() / mseq::kLanes;
    int lane = (int)((e.mousePosition.y - r.top) / rowH);
    lane = std::max(0, std::min(mseq::kLanes - 1, lane));
    if (e.mousePosition.x < r.left + kEdgeW + 6) { if (mute_) mute_(lane); }
    else if (select_) select_(lane);
    e.consumed = true;
}

//---------------------------------------------------------------- song strip
CRect SongStrip::cellOf(int s) const {
    const CRect r = getViewSize();
    const CCoord w = (r.getWidth() - (mseq::kSections - 1) * 5) / mseq::kSections;
    const CCoord x = r.left + s * (w + 5);
    return CRect(x, r.top, x + w, r.bottom);
}

void SongStrip::setState(const int* bars, const bool* lanes, int current) {
    for (int s = 0; s < mseq::kSections; s++) bars_[s] = bars[s];
    for (int i = 0; i < mseq::kSections * mseq::kLanes; i++) lanes_[i] = lanes[i];
    cur_ = current;
    invalid();
}

void SongStrip::draw(CDrawContext* c) {
    c->setDrawMode(kAntiAliasing);
    c->setFont(font_);
    for (int s = 0; s < mseq::kSections; s++) {
        const CRect cell = cellOf(s);
        c->setFillColor(s == cur_ ? kCurBg : kCellBg);
        c->drawRect(cell, kDrawFilled);

        char n[8];
        if (bars_[s] <= 0) snprintf(n, sizeof n, "-");
        else               snprintf(n, sizeof n, "%d", bars_[s]);
        c->setFontColor(bars_[s] <= 0 ? CColor(51, 50, 59, 255)
                                      : (s == cur_ ? kLaneColour[0] : kInk));
        c->drawString(n, CRect(cell.left + 8, cell.top, cell.left + 28, cell.bottom),
                      kLeftText);

        for (int l = 0; l < mseq::kLanes; l++) {
            const CCoord dx = cell.right - 8 - (mseq::kLanes - l) * 7;
            const CCoord dy = cell.top + cell.getHeight() * 0.5 - 2;
            const bool on = lanes_[s * mseq::kLanes + l];
            c->setFillColor(on ? kLaneColour[l] : CColor(37, 38, 46, 255));
            c->drawRect(CRect(dx, dy, dx + 5, dy + 5), kDrawFilled);
        }
    }
}

void SongStrip::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft()) return;
    for (int s = 0; s < mseq::kSections; s++) {
        const CRect cell = cellOf(s);
        if (!cell.pointInside(e.mousePosition)) continue;
        // the dots on the right toggle lanes, the numeral drags the bar count
        const CCoord dotsFrom = cell.right - 8 - mseq::kLanes * 7 - 3;
        if (e.mousePosition.x >= dotsFrom) {
            int l = (int)((e.mousePosition.x - (cell.right - 8 - mseq::kLanes * 7)) / 7);
            l = std::max(0, std::min(mseq::kLanes - 1, l));
            if (toggle_) toggle_(s, l);
        } else {
            drag_ = s;
            lastY_ = e.mousePosition.y;
            if (edit_) edit_(mseq::sectionParam(s, mseq::kSectionBars), 0.0, 0);
        }
        e.consumed = true;
        return;
    }
}

void SongStrip::onMouseMoveEvent(MouseMoveEvent& e) {
    if (drag_ < 0 || !edit_) return;
    const double d = (lastY_ - e.mousePosition.y) / 140.0;
    lastY_ = e.mousePosition.y;
    edit_(mseq::sectionParam(drag_, mseq::kSectionBars), d, 1);
    e.consumed = true;
}

void SongStrip::onMouseUpEvent(MouseUpEvent& e) {
    if (drag_ < 0) return;
    if (edit_) edit_(mseq::sectionParam(drag_, mseq::kSectionBars), 0.0, 2);
    drag_ = -1;
    e.consumed = true;
}

//----------------------------------------------------------------- the pads
void StrokePads::setEdited(const bool* e) {
    for (int i = 0; i < mseq::kNumStrokes; i++) edited_[i] = e[i];
    invalid();
}

void StrokePads::draw(CDrawContext* c) {
    const CRect r = getViewSize();
    const CCoord w = (r.getWidth() - (mseq::kNumStrokes - 1) * 4) / mseq::kNumStrokes;
    c->setDrawMode(kAntiAliasing);
    c->setFont(mono_);
    for (int i = 0; i < mseq::kNumStrokes; i++) {
        const CCoord x = r.left + i * (w + 4);
        const CRect cell(x, r.top, x + w, r.bottom);
        c->setFillColor(i == sel_ ? kLaneColour[0] : kCellBg);
        c->drawRect(cell, kDrawFilled);
        c->setFontColor(i == sel_ ? kBg : kLab);
        char n[4] = {mseq::kStrokeChars[i], 0, 0, 0};
        c->drawString(n, cell, kCenterText);
        // an edited stroke wears a mark, so you can see which ones you changed
        if (edited_[i]) {
            c->setFillColor(i == sel_ ? kBg : kLaneColour[0]);
            c->drawRect(CRect(cell.right - 7, cell.top + 3,
                              cell.right - 3, cell.top + 7), kDrawFilled);
        }
    }
}

void StrokePads::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft()) return;
    const CRect r = getViewSize();
    const CCoord w = (r.getWidth() - (mseq::kNumStrokes - 1) * 4) / mseq::kNumStrokes;
    int i = (int)((e.mousePosition.x - r.left) / (w + 4));
    i = std::max(0, std::min(mseq::kNumStrokes - 1, i));
    if (pick_) pick_(i);
    e.consumed = true;
}

//--------------------------------------------------------------------- layout
// The window is one size for every page, so its height is set by the tallest -
// the patch page, which has to fit six operator rows without squeezing them.
// The sequence page spends the extra room on taller lane rows rather than
// leaving a hole, which it wanted anyway.
static const int kEdW    = 900;
static const int kMar    = 20;
static const int kTopY   = 15;
static const int kGridY  = 54;

// sequence page
static const int kLaneH  = 58;
static const int kGridH  = mseq::kLanes * kLaneH;
static const int kSongY  = kGridY + kGridH + 18;
static const int kSongH  = 40;
static const int kHdrY   = kSongY + kSongH + 18;
static const int kHdrH   = 24;
static const int kParamY = kHdrY + kHdrH + 16;
static const int kParamH = 54;

// patch page: pads, then the operator rows, then its own parameter row. These
// are laid out independently of the sequence page because the two pages never
// share a view, and the operator rows need every pixel they can get.
static const int kPadsY  = kGridY;
static const int kPadsH  = 30;
static const int kOpsY   = kPadsY + kPadsH + 12;
static const int kOpRowH = 58;
static const int kOpsH   = 6 * kOpRowH;
static const int kPRowY  = kOpsY + kOpsH + 16;
static const int kPRowH  = 54;

static const int kBotY   = kPRowY + kPRowH + 18;
static const int kBotH   = 24;
static const int kHelpY  = kBotY + kBotH + 12;
static const int kEdH    = kHelpY + 22;

SeqEditor::SeqEditor(void* controller, SeqVST* plug)
    : VSTGUIEditor(controller), plug_(plug) {
    // The size the host is told about, before it ever asks for the window.
    // Truncated, not rounded, to match what VSTGUIEditor::attached derives
    // from the zoomed frame - otherwise the two disagree by a pixel and the
    // host is asked to resize the moment it finishes opening.
    const double uiScale = mplat::editorScale();
    ViewRect vr(0, 0, (int32)(kEdW * uiScale), (int32)(kEdH * uiScale));
    setRect(vr);
}

CTextLabel* SeqEditor::makeLabel(const CRect& r, const char* text,
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

void SeqEditor::styleButton(CTextButton* b) {
    b->setRoundRadius(2);
    b->setFrameWidth(1);
    b->setFrameColor(kRule);
    b->setFrameColorHighlighted(kLaneColour[0]);
    b->setTextColor(kLab);
    b->setTextColorHighlighted(kLaneColour[0]);
    b->setFont(labelFont_);
    auto flat = VSTGUI::owned(CGradient::create(0., 1., kCellBg, kCellBg));
    auto lit  = VSTGUI::owned(CGradient::create(0., 1., kSelBg, kSelBg));
    b->setGradient(flat);
    b->setGradientHighlighted(lit);
}

CTextButton* SeqEditor::addAction(const CRect& r, int32_t tag, const char* label,
                                  const char* help) {
    auto* b = new CTextButton(r, this, tag, label, CTextButton::kKickStyle);
    styleButton(b);
    if (help) b->setTooltipText(help);
    frame->addView(b);
    return b;
}

COptionMenu* SeqEditor::addMenu(const CRect& r, int paramId) {
    const ParamDef& d = params()[paramId];
    auto* m = new COptionMenu(r, this, paramId);
    for (int i = 0; i < d.numNames; i++) m->addEntry(d.names[i]);
    m->setMin(0.f);
    m->setMax((float)(d.numNames - 1));
    m->setBackColor(kCellBg);
    m->setFrameColor(kRule);
    m->setFontColor(kInk);
    m->setFont(valueFont_);
    m->setHoriAlign(kLeftText);
    frame->addView(m);
    bound_.push_back(m);
    attachHelp(m, paramId);
    return m;
}

bool PLUGIN_API SeqEditor::open(void* parent, const PlatformType& type) {
    CRect size(0, 0, kEdW, kEdH);
    frame = new CFrame(size, this);
    frame->setBackgroundColor(kBg);
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
    for (int i = 0; i < 3; i++) pageViews_[i].clear();

    // ---- header: the name, what it is doing, and the set ----
    {
        auto* t = makeLabel(CRect(kMar, kTopY, kMar + 220, kTopY + 18),
                            "M A G H R E B   S E Q", kInk, kLeftText, true);
        t->setFont(titleFont_);
        t->setMouseEnabled(false);
        frame->addView(t);

        status_ = makeLabel(CRect(kMar + 236, kTopY, kMar + 560, kTopY + 18), "",
                            kDim, kLeftText, true);
        status_->setMouseEnabled(false);
        frame->addView(status_);

        setName_ = makeLabel(CRect(kEdW - kMar - 356, kTopY - 1,
                                   kEdW - kMar - 196, kTopY + 19),
                             "init", kLab, kRightText, true);
        setName_->setMouseEnabled(false);
        frame->addView(setName_);

        const int by = kTopY - 2;
        addAction(CRect(kEdW - kMar - 188, by, kEdW - kMar - 166, by + 20),
                  kSetPrevId, "<", "Previous set in the folder.");
        addAction(CRect(kEdW - kMar - 164, by, kEdW - kMar - 142, by + 20),
                  kSetNextId, ">", "Next set in the folder.");
        addAction(CRect(kEdW - kMar - 134, by, kEdW - kMar - 88, by + 20),
                  kSetSaveId, "SAVE",
                  "Save this set: every parameter, the kit, and each lane's cartridge voice.");
        addAction(CRect(kEdW - kMar - 86, by, kEdW - kMar - 40, by + 20),
                  kSetLoadId, "LOAD", "Load a set.");
        addAction(CRect(kEdW - kMar - 38, by, kEdW - kMar, by + 20),
                  kSetInitId, "INIT", "Back to the factory state.");
    }

    // ---- page 1: the lanes ----
    grid_ = new LaneGrid(CRect(kMar, kGridY, kEdW - kMar, kGridY + kGridH), valueFont_,
                         [this](int lane) { selectLane(lane); },
                         [this](int lane) {
                             if (!plug_) return;
                             const int p = mseq::laneParam(lane, mseq::kLaneMute);
                             const double v = plug_->getParamNormalized((ParamID)p);
                             plug_->setParamFromUI(p, v >= 0.5 ? 0.0 : 1.0);
                             refreshValues();
                             refreshSet();
                         });
    frame->addView(grid_);
    pageViews_[0].push_back(grid_);

    {
        const int ty = kSongY + (kSongH - 22) / 2;
        songBtn_ = new CTextButton(CRect(kMar, ty, kMar + 54, ty + 22), this,
                                   mseq::kSongOn, "SONG", CTextButton::kOnOffStyle);
        styleButton(songBtn_);
        songBtn_->setTooltipText(params()[mseq::kSongOn].help.c_str());
        frame->addView(songBtn_);
        bound_.push_back(songBtn_);
        pageViews_[0].push_back(songBtn_);
        attachHelp(songBtn_, mseq::kSongOn);

        loopBtn_ = new CTextButton(CRect(kMar + 58, ty, kMar + 112, ty + 22), this,
                                   mseq::kSongLoop, "LOOP", CTextButton::kOnOffStyle);
        styleButton(loopBtn_);
        loopBtn_->setTooltipText(params()[mseq::kSongLoop].help.c_str());
        frame->addView(loopBtn_);
        bound_.push_back(loopBtn_);
        pageViews_[0].push_back(loopBtn_);
        attachHelp(loopBtn_, mseq::kSongLoop);
    }
    song_ = new SongStrip(CRect(kMar + 124, kSongY, kEdW - kMar, kSongY + kSongH), valueFont_,
                          [this](int p, double d, int ph) { nudgeParam(p, d, ph); },
                          [this](int s, int l) {
                              if (!plug_) return;
                              const int p = mseq::sectionParam(s, mseq::kSectionL1 + l);
                              const double v = plug_->getParamNormalized((ParamID)p);
                              plug_->setParamFromUI(p, v >= 0.5 ? 0.0 : 1.0);
                              refreshSong();
                              refreshSet();
                          });
    frame->addView(song_);
    pageViews_[0].push_back(song_);

    // ---- page 1: the selected lane's rhythm, kit and mute ----
    {
        rhythmMenu_ = addMenu(CRect(kMar, kHdrY, kMar + 250, kHdrY + kHdrH),
                              mseq::laneParam(0, mseq::kLaneRhythm));
        kitMenu_ = addMenu(CRect(kMar + 258, kHdrY, kMar + 366, kHdrY + kHdrH),
                           mseq::laneParam(0, mseq::kLaneKit));
        muteBtn_ = new CTextButton(CRect(kMar + 374, kHdrY, kMar + 428, kHdrY + kHdrH),
                                   this, mseq::laneParam(0, mseq::kLaneMute), "MUTE",
                                   CTextButton::kOnOffStyle);
        styleButton(muteBtn_);
        frame->addView(muteBtn_);
        bound_.push_back(muteBtn_);

        pageViews_[0].push_back(rhythmMenu_);
        pageViews_[0].push_back(kitMenu_);
        pageViews_[0].push_back(muteBtn_);

        voiceName_ = makeLabel(CRect(kMar + 446, kHdrY, kMar + 700, kHdrY + kHdrH), "",
                               kDim, kLeftText, true);
        voiceName_->setMouseEnabled(false);
        frame->addView(voiceName_);
        pageViews_[0].push_back(voiceName_);
    }

    prow_ = new ParamRow(CRect(kMar, kParamY, kEdW - kMar, kParamY + kParamH),
                         uiPalette(), labelFont_, bigFont_,
                         [this](int p, double d, int ph) { nudgeParam(p, d, ph); });
    frame->addView(prow_);
    pageViews_[0].push_back(prow_);

    // ---- page 2: the six-operator patch ----
    {
        auto* sl = makeLabel(CRect(kMar, kPadsY - 24, kMar + 60, kPadsY - 6),
                             "STROKE", kDim, kLeftText, true);
        sl->setMouseEnabled(false);
        frame->addView(sl);
        pageViews_[1].push_back(sl);

        patchState_ = makeLabel(CRect(kMar + 300, kPadsY - 24, kEdW - kMar - 120,
                                      kPadsY - 6), "", kDim, kRightText, true);
        patchState_->setMouseEnabled(false);
        frame->addView(patchState_);
        pageViews_[1].push_back(patchState_);

        pageViews_[1].push_back(addAction(
            CRect(kEdW - kMar - 100, kPadsY - 26, kEdW - kMar, kPadsY - 4),
            kRevertId, "REVERT",
            "Throw the edits away and go back to the patch derived from this stroke."));

        pads_ = new StrokePads(CRect(kMar, kPadsY, kEdW - kMar, kPadsY + kPadsH),
                               valueFont_, [this](int st) { selectStroke(st); });
        frame->addView(pads_);
        pageViews_[1].push_back(pads_);

        ops_ = new OpRows(CRect(kMar, kOpsY, kEdW - kMar, kOpsY + kOpsH),
                          uiPalette(), valueFont_, bigFont_,
                          [this](int op) { selectOp(op); });
        frame->addView(ops_);
        pageViews_[1].push_back(ops_);

        patchRow_ = new ParamRow(CRect(kMar, kPRowY, kEdW - kMar, kPRowY + kPRowH),
                                 uiPalette(), labelFont_, bigFont_,
                                 [this](int id, double d, int ph) {
                                     if (ph == 1) editPatch(id, d);
                                 });
        frame->addView(patchRow_);
        pageViews_[1].push_back(patchRow_);

        pageViews_[1].push_back(addAction(
            CRect(kEdW - kMar - 92, kBotY, kEdW - kMar - 48, kBotY + kBotH),
            kPatchPageId, "1/2", "The other eight values: levels, mode, feedback and algorithm."));
    }

    // ---- page 3: the kit and the cartridge ----
    {
        mdx::BrowserPalette bp{kCellBg, kSelBg, kRule, kInk, kLab, kLaneColour[0], kCurBg};
        browser_ = new mdx::CartPanel(
            CRect(kMar, kGridY, kEdW - kMar, kGridY + 186), bp, labelFont_, valueFont_);
        browser_->setPick([this](const std::string& path, int voice,
                                 const std::string& label) {
            if (!plug_) return;
            if (plug_->pickLaneVoice(path.c_str(), voice, lane_)) {
                char b[224];
                snprintf(b, sizeof b, "Lane %d plays \"%s\".", lane_ + 1, label.c_str());
                setHelp(b);
            } else {
                setHelp("That cartridge would not read.");
            }
            plug_->markDirty();
            showVoiceName();
            refreshSet();
        });
        // This plug-in's own pool, not one shared with the others.
        browser_->setFavouritesFile(
            mplat::userDataFolder("MaghrebSeq", "Favourites") + "/favourites.txt");
        browser_->rescan(SeqVST::cartridgeFolder());
        frame->addView(browser_);
        pageViews_[2].push_back(browser_);

        const int y = kGridY + 200;
        auto* kl = makeLabel(CRect(kMar, y, kMar + 40, y + 20), "KIT", kDim, kLeftText, true);
        kl->setMouseEnabled(false);
        frame->addView(kl);
        pageViews_[2].push_back(kl);

        kitName_ = makeLabel(CRect(kMar + 46, y, kMar + 300, y + 20), "FACTORY",
                             kInk, kLeftText, true);
        kitName_->setMouseEnabled(false);
        frame->addView(kitName_);
        pageViews_[2].push_back(kitName_);

        pageViews_[2].push_back(addAction(CRect(kMar + 310, y, kMar + 382, y + 20),
            kLoadKitId, "LOAD",
            "Load a .mkit written by Maghreb FM or Maghreb DX. Both engines play it."));
        pageViews_[2].push_back(addAction(CRect(kMar + 390, y, kMar + 474, y + 20),
            kClearKitId, "FACTORY", "Put the module's own strokes back."));
        pageViews_[2].push_back(addAction(CRect(kMar + 500, y, kMar + 624, y + 20),
            kClearVoiceId, "LANE DERIVED",
            "Put the selected lane back on the patch derived from its strokes."));
    }

    // ---- the pages ----
    {
        const char* names[3] = {"SEQ", "PATCH", "KIT"};
        const int32_t tg[3] = {kPageSeqId, kPagePatchId, kPageKitId};
        for (int i = 0; i < 3; i++) {
            tabs_[i] = new CTextButton(CRect(kMar + i * 62, kBotY,
                                             kMar + i * 62 + 56, kBotY + kBotH),
                                       this, tg[i], names[i], CTextButton::kKickStyle);
            styleButton(tabs_[i]);
            frame->addView(tabs_[i]);
        }
    }

    // ---- the three ways out ----
    //
    // A rhythm is a description before it is a noise. These write it down as
    // MIDI to play anything with, render it as a loop, or render the strokes
    // as a kit of one shots - all three from the same walk of the pattern the
    // sequencer plays, so none of them can drift from what is heard.
    {
        struct { int32_t tag; const char* label; int w; const char* tip; } outs[] = {
            { kWriteMidiId,  "WRITE MIDI",  100,
              "Writes the pattern as a MIDI file, on the notes Maghreb DX and "
              "Tubu DX already answer to - so it plays them, or any drum "
              "sampler, without a mapping in between." },
            { kRenderLoopId, "RENDER LOOP", 110,
              "Renders one turn of the pattern to a WAV, at the tempo the host "
              "is on, with the tail it rings out into." },
            { kRenderKitId,  "RENDER KIT",  105,
              "Renders each of the eleven strokes on its own, named by the note "
              "it answers to, so a sampler lays them out in pad order." },
        };
        int x = kEdW - kMar - (100 + 110 + 105 + 16);
        for (const auto& o : outs) {
            auto* b = new CTextButton(CRect(x, kBotY, x + o.w, kBotY + kBotH),
                                      this, o.tag, o.label, CTextButton::kKickStyle);
            styleButton(b);
            b->setTooltipText(o.tip);
            frame->addView(b);
            x += o.w + 8;
        }
    }

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
    showPage(0);
    selectLane(0);
    showKitName();
    refreshSong();
    refreshSet();
    setIdleRate(40);
    return true;
}

void PLUGIN_API SeqEditor::close() {
    if (plug_) plug_->setEditor(nullptr);
    if (frame) frame->unregisterMouseObserver(this);
    bound_.clear();
    helpFor_.clear();
    for (int i = 0; i < 3; i++) pageViews_[i].clear();
    grid_ = nullptr; song_ = nullptr; prow_ = nullptr; browser_ = nullptr;
    ops_ = nullptr; pads_ = nullptr; patchRow_ = nullptr; patchState_ = nullptr;
    songBtn_ = nullptr; loopBtn_ = nullptr;
    status_ = nullptr; help_ = nullptr; setName_ = nullptr;
    kitName_ = nullptr; voiceName_ = nullptr;
    rhythmMenu_ = nullptr; kitMenu_ = nullptr; muteBtn_ = nullptr;
    tabs_[0] = tabs_[1] = tabs_[2] = nullptr;
    labelFont_ = nullptr; valueFont_ = nullptr; bigFont_ = nullptr; titleFont_ = nullptr;
    if (frame) { frame->forget(); frame = nullptr; mplat::closeEditorFrame(); }
}

void SeqEditor::showPage(int page) {
    page_ = std::max(0, std::min(2, page));
    for (int p = 0; p < 3; p++)
        for (CView* v : pageViews_[p]) if (v) v->setVisible(p == page_);
    for (int i = 0; i < 3; i++)
        if (tabs_[i]) {
            tabs_[i]->setTextColor(i == page_ ? kInk : kLab);
            tabs_[i]->setFrameColor(i == page_ ? kLaneColour[0] : kRule);
            tabs_[i]->invalid();
        }
    if (page_ == 1) refreshPatch();
    if (frame) frame->invalid();
}

void SeqEditor::selectLane(int lane) {
    lane_ = std::max(0, std::min(mseq::kLanes - 1, lane));
    if (grid_) grid_->setSelected(lane_);
    // The menus and the mute follow the selection rather than there being four
    // of each. That is what removes three quarters of the old control strip.
    if (rhythmMenu_) rhythmMenu_->setTag(mseq::laneParam(lane_, mseq::kLaneRhythm));
    if (kitMenu_)    kitMenu_->setTag(mseq::laneParam(lane_, mseq::kLaneKit));
    if (muteBtn_)    muteBtn_->setTag(mseq::laneParam(lane_, mseq::kLaneMute));
    refreshValues();
    showVoiceName();
}

void SeqEditor::nudgeParam(int param, double delta, int phase) {
    if (!plug_ || param < 0 || param >= kTotalParams) return;
    if (phase == 0) { VSTGUIEditor::beginEdit(param); return; }
    if (phase == 2) { VSTGUIEditor::endEdit(param); return; }
    double v = plug_->getParamNormalized((ParamID)param) + delta;
    v = std::max(0.0, std::min(1.0, v));
    plug_->setParamFromUI(param, v);
    refreshValues();
    refreshSong();
    refreshSet();
}

void SeqEditor::setHelp(const char* text) {
    if (!help_) return;
    help_->setText(text && *text ? text
        : "Click a lane to select it, its coloured edge to mute it. Drag a value to change it.");
}

void SeqEditor::attachHelp(CView* v, int paramId) {
    if (!v || paramId < 0 || paramId >= kTotalParams) return;
    helpFor_[v] = paramId;
    v->setTooltipText(params()[paramId].help.c_str());
}

void SeqEditor::onMouseEntered(CView* view, CFrame*) {
    auto it = helpFor_.find(view);
    setHelp(it != helpFor_.end() ? params()[it->second].help.c_str() : nullptr);
}
void SeqEditor::onMouseExited(CView* view, CFrame*) {
    if (helpFor_.find(view) != helpFor_.end()) setHelp(nullptr);
}

void SeqEditor::beginEdit(int32_t i) { if (i < kUiTagBase) VSTGUIEditor::beginEdit(i); }
void SeqEditor::endEdit(int32_t i)   { if (i < kUiTagBase) VSTGUIEditor::endEdit(i); }

void SeqEditor::stepSet(int dir) {
    if (!plug_) return;
    const std::vector<std::string> all = SeqVST::listSets();
    if (all.empty()) { setHelp("No sets saved yet - SAVE writes the first one."); return; }
    int at = -1;
    for (int i = 0; i < (int)all.size(); i++)
        if (all[i] == plug_->setPath()) { at = i; break; }
    const int n = (int)all.size();
    const int next = (at < 0) ? (dir > 0 ? 0 : n - 1) : ((at + dir) % n + n) % n;
    if (plug_->loadSet(all[next].c_str())) {
        setHelp("Set loaded.");
        showKitName(); showVoiceName();
        refreshValues(); refreshSong(); refreshSet();
    } else {
        setHelp("That set would not load.");
    }
}

void SeqEditor::valueChanged(CControl* c) {
    if (!c || !plug_) return;
    const int32_t tag = c->getTag();

    if (tag == kPageSeqId || tag == kPagePatchId || tag == kPageKitId) {
        if (c->getValue() < 0.5f) return;
        showPage(tag == kPageSeqId ? 0 : (tag == kPagePatchId ? 1 : 2));
        return;
    }
    if (tag == kRevertId) {
        if (c->getValue() < 0.5f) return;
        plug_->revertStroke(stroke_);
        setHelp("Back to the patch derived from this stroke.");
        refreshPatch(); refreshSet();
        return;
    }
    if (tag == kPatchPageId) {
        if (c->getValue() < 0.5f) return;
        patchPage_ = 1 - patchPage_;
        refreshPatch();
        return;
    }
    if (tag == kSetPrevId || tag == kSetNextId) {
        if (c->getValue() < 0.5f) return;
        stepSet(tag == kSetNextId ? 1 : -1);
        return;
    }
    if (tag == kWriteMidiId || tag == kRenderLoopId || tag == kRenderKitId) {
        if (c->getValue() < 0.5f || !plug_) return;      // kick buttons fire twice
        std::string err;
        char msg[300];

        if (tag == kRenderKitId) {
            const int n = plug_->exportKit(&err);
            if (n > 0)
                snprintf(msg, sizeof msg,
                         "Rendered %d strokes into the Exports folder, named by note.", n);
            else
                snprintf(msg, sizeof msg, "%s",
                         err.empty() ? "Nothing to render." : err.c_str());
        } else {
            const std::string path = (tag == kWriteMidiId) ? plug_->exportMidi(&err)
                                                           : plug_->exportAudio(&err);
            if (!path.empty()) {
                const size_t cut = path.find_last_of("/\\");
                snprintf(msg, sizeof msg, "Wrote %s into the Exports folder.",
                         (cut == std::string::npos ? path : path.substr(cut + 1)).c_str());
            } else {
                snprintf(msg, sizeof msg, "%s",
                         err.empty() ? "Nothing to write." : err.c_str());
            }
        }
        setHelp(msg);
        return;
    }

    if (tag == kSetInitId) {
        if (c->getValue() < 0.5f) return;
        plug_->initSet();
        showKitName(); showVoiceName();
        refreshValues(); refreshSong(); refreshSet();
        setHelp("Back to the factory state.");
        return;
    }
    if (tag == kSetSaveId || tag == kSetLoadId) {
        if (c->getValue() < 0.5f) return;
        const bool saving = (tag == kSetSaveId);
        auto* sel = CNewFileSelector::create(frame, saving
            ? CNewFileSelector::kSelectSaveFile : CNewFileSelector::kSelectFile);
        if (!sel) return;
        sel->setTitle(saving ? "Save this set" : "Load a set");
        sel->setInitialDirectory(SeqVST::setFolder().c_str());
        const CFileExtension ext("Maghreb Seq set", "mseq");
        sel->addFileExtension(ext);
        if (saving) { sel->setDefaultExtension(ext); sel->setDefaultSaveName("My Set.mseq"); }
        sel->run([this, saving](CNewFileSelector* s) {
            if (s->getNumSelectedFiles() == 0 || !plug_) return;
            const char* path = s->getSelectedFile(0);
            const bool done = saving ? plug_->saveSet(path) : plug_->loadSet(path);
            setHelp(done ? (saving ? "Set saved." : "Set loaded.")
                         : "Could not open that file.");
            showKitName(); showVoiceName();
            refreshValues(); refreshSong(); refreshSet();
        });
        sel->forget();
        return;
    }
    if (tag == kLoadKitId) {
        if (c->getValue() < 0.5f) return;
        auto* sel = CNewFileSelector::create(frame, CNewFileSelector::kSelectFile);
        if (!sel) return;
        sel->setTitle("Load a kit");
        sel->setInitialDirectory(SeqVST::kitFolder().c_str());
        sel->addFileExtension(CFileExtension("Maghreb kit", "mkit"));
        sel->run([this](CNewFileSelector* s) {
            if (s->getNumSelectedFiles() == 0 || !plug_) return;
            const bool done = plug_->loadKit(s->getSelectedFile(0));
            setHelp(done ? "Kit loaded. Both engines play it."
                         : "That file has no strokes in it.");
            plug_->markDirty();
            showKitName(); refreshSet();
        });
        sel->forget();
        return;
    }
    if (tag == kClearKitId) {
        if (c->getValue() < 0.5f) return;
        plug_->clearKit();
        plug_->markDirty();
        setHelp("Factory strokes restored.");
        showKitName(); refreshSet();
        return;
    }
    if (tag == kClearVoiceId) {
        if (c->getValue() < 0.5f) return;
        plug_->clearLaneVoice(lane_);
        plug_->markDirty();
        setHelp("That lane is back on its derived patch.");
        showVoiceName(); refreshSet();
        return;
    }

    if (tag < 0 || tag >= kTotalParams) return;
    double nv = c->getValue();
    const ParamDef& d = params()[tag];
    if (d.names && d.numNames > 1) nv = nv / (double)(d.numNames - 1);
    plug_->setParamFromUI(tag, nv);
    refreshValues();
    refreshSet();
}

void SeqEditor::syncControl(int paramId, double normalized) {
    for (size_t i = 0; i < bound_.size(); i++) {
        CControl* c = bound_[i];
        if (!c || c->getTag() != paramId) continue;
        const ParamDef& d = params()[paramId];
        c->setValue(d.names ? (float)(normalized * (d.numNames - 1)) : (float)normalized);
        c->invalid();
    }
    refreshValues();
}

// The eight cells are the selected lane's five, then the three that belong to
// the whole kit. Tone and Vel>FM are voice controls and live on the patch page.
void SeqEditor::refreshValues() {
    if (!plug_ || !prow_) return;

    struct Def { int id; const char* label; bool lane; };
    static const Def defs[8] = {
        { mseq::kLaneLevel, "LEVEL",  true  },
        { mseq::kLanePan,   "PAN",    true  },
        { mseq::kLaneTune,  "TUNE",   true  },
        { mseq::kLaneDecay, "DECAY",  true  },
        { mseq::kLaneDrive, "DRIVE",  true  },
        { mseq::kSwing,     "SWING",  false },
        { mseq::kMaster,    "MASTER", false },
        { mseq::kOutput,    "OUT",    false },
    };

    std::vector<ParamRow::Cell> cells;
    cells.reserve(8);
    for (int i = 0; i < 8; i++) {
        ParamRow::Cell cell;
        cell.param = defs[i].lane ? mseq::laneParam(lane_, defs[i].id) : defs[i].id;
        cell.label = defs[i].label;
        cell.norm = (float)plug_->getParamNormalized((ParamID)cell.param);
        cell.bar = defs[i].lane ? kLaneColour[lane_] : kLab;

        String128 str = {0};
        if (plug_->getParamStringByValue((ParamID)cell.param, cell.norm, str) == kResultOk) {
            cell.value = toAscii(str);
            const ParamDef& d = params()[cell.param];
            if (!d.unit.empty()) { cell.value += " "; cell.value += d.unit; }
        }
        cells.push_back(cell);
    }
    prow_->setCells(cells);

    if (muteBtn_) {
        muteBtn_->setValue((float)plug_->getParamNormalized(
            (ParamID)mseq::laneParam(lane_, mseq::kLaneMute)));
        muteBtn_->invalid();
    }
    if (songBtn_) {
        songBtn_->setValue((float)plug_->getParamNormalized((ParamID)mseq::kSongOn));
        songBtn_->invalid();
    }
    if (loopBtn_) {
        loopBtn_->setValue((float)plug_->getParamNormalized((ParamID)mseq::kSongLoop));
        loopBtn_->invalid();
    }
    COptionMenu* menus[2] = {rhythmMenu_, kitMenu_};
    for (COptionMenu* m : menus) {
        if (!m) continue;
        const ParamDef& d = params()[m->getTag()];
        const double nv = plug_->getParamNormalized((ParamID)m->getTag());
        m->setValue(d.names ? (float)(nv * (d.numNames - 1)) : (float)nv);
        m->invalid();
    }
}

void SeqEditor::refreshSong() {
    if (!plug_ || !song_) return;
    int bars[mseq::kSections];
    bool lanes[mseq::kSections * mseq::kLanes];
    for (int s = 0; s < mseq::kSections; s++) {
        const int bp = mseq::sectionParam(s, mseq::kSectionBars);
        const ParamDef& d = params()[bp];
        const double nv = plug_->getParamNormalized((ParamID)bp);
        bars[s] = (int)(d.lo + nv * (d.hi - d.lo) + 0.5);
        for (int l = 0; l < mseq::kLanes; l++)
            lanes[s * mseq::kLanes + l] = plug_->getParamNormalized(
                (ParamID)mseq::sectionParam(s, mseq::kSectionL1 + l)) >= 0.5;
    }
    song_->setState(bars, lanes, plug_->snapshot().section);
}


//------------------------------------------------------------ the patch page
//
// The eight cells are patch bytes, not VST3 parameters - there are 156 of them
// per stroke and eleven strokes, which is no more automatable than a sample
// is. They travel in the Set instead.
namespace {
struct PatchField {
    const char* label;
    int off;          // byte inside the operator block, or -1 for a global
    int global;       // byte index in the patch when off < 0
    int lo, hi;
};
// page 0: what the operator sounds like. page 1: its envelope levels and the
// two bytes that belong to the voice rather than the operator.
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

void SeqEditor::selectStroke(int stroke) {
    stroke_ = std::max(0, std::min(mseq::kNumStrokes - 1, stroke));
    if (pads_) pads_->setSelected(stroke_);
    refreshPatch();
    if (plug_) plug_->auditionStroke(stroke_);
}

void SeqEditor::selectOp(int op) {
    op_ = std::max(0, std::min(5, op));
    if (ops_) ops_->setSelected(op_);
    refreshPatch();
}

void SeqEditor::editPatch(int cellId, double delta) {
    if (!plug_ || cellId < kPatchBase) return;
    const int idx = cellId - kPatchBase;
    const int page = idx / 8, field = idx % 8;
    if (page < 0 || page > 1 || field < 0 || field > 7) return;
    const PatchField& f = kPatchFields[page][field];

    uint8_t patch[156];
    plug_->patchFor(stroke_, patch);
    const int at = fieldByte(f, op_);
    const int span = std::max(1, f.hi - f.lo);
    // a whole sweep of the cell covers the field's own range, so a 0..1 byte
    // is not impossible to hit and a 0..99 one is not glacial
    const int now = patch[at];
    const int want = (int)std::lround(now + delta * span * 1.6);
    if (want == now) return;
    plug_->setPatchByte(stroke_, at, want, f.lo, f.hi);
    refreshPatch();
    refreshSet();
}

void SeqEditor::refreshPatch() {
    if (!plug_) return;
    uint8_t patch[156];
    plug_->patchFor(stroke_, patch);

    if (ops_) {
        std::vector<OpRows::Op> rows(6);
        for (int i = 0; i < 6; i++) {
            const uint8_t* o = patch + (5 - i) * 21;
            OpRows::Op& r = rows[i];
            r.colour = kOpColour[i];
            r.on = o[16] > 0;
            r.fb = (i == 5) && patch[135] > 0;
            for (int k = 0; k < 4; k++) {
                r.lev[k] = o[4 + k] / 99.f;
                r.rt[k]  = o[k] / 99.f;
            }

            char b[24];
            if (o[17]) {
                // fixed mode: coarse is a decade, fine the mantissa
                const double hz = std::pow(10.0, (o[18] & 3) + o[19] / 100.0);
                if (hz >= 1000.0) snprintf(b, sizeof b, "%.1fk", hz / 1000.0);
                else              snprintf(b, sizeof b, "%.0f", hz);
            } else {
                const double ratio = (o[18] == 0 ? 0.5 : (double)o[18]) *
                                     (1.0 + o[19] / 100.0);
                snprintf(b, sizeof b, "%.2f", ratio);
            }
            r.ratio = b;
            r.haveWave = plug_->operatorWave(stroke_, i, r.wave, 192);
        }
        // The wiring comes out of msfa's own algorithm table, so a cartridge
        // patch on any of the 32 draws correctly rather than only algorithm 5.
        // msfa numbers its operators the other way round - its 0 is OP6 - so
        // both the targets and the carrier flags are flipped into row order.
        int tgt[6]; bool car[6]; int fbOp = -1;
        mdx::DxEngine::algorithmRouting(patch[134], tgt, car, fbOp);
        for (int i = 0; i < 6; i++) {
            const int m = 5 - i;                     // row i is msfa index 5 - i
            rows[i].carrier = car[m];
            rows[i].target = (tgt[m] < 0) ? -1 : (5 - tgt[m]);
            rows[i].fb = (fbOp == m);
        }

        ops_->setOps(rows);
        ops_->setSelected(op_);
    }

    if (pads_) {
        bool ed[mseq::kNumStrokes];
        for (int i = 0; i < mseq::kNumStrokes; i++) ed[i] = plug_->strokeEdited(i);
        pads_->setEdited(ed);
    }

    if (patchRow_) {
        std::vector<ParamRow::Cell> cells;
        cells.reserve(8);
        for (int i = 0; i < 8; i++) {
            const PatchField& f = kPatchFields[patchPage_][i];
            ParamRow::Cell cell;
            cell.param = kPatchBase + patchPage_ * 8 + i;
            cell.label = f.label;
            const int v = patch[fieldByte(f, op_)];
            cell.norm = (float)(v - f.lo) / (float)std::max(1, f.hi - f.lo);
            cell.bar = kOpColour[op_];
            char b[24];
            if (f.off == 20)      snprintf(b, sizeof b, "%+d", v - 7);   // detune
            else if (f.off == 17) snprintf(b, sizeof b, "%s", v ? "FIX" : "RAT");
            else if (f.global == 134) snprintf(b, sizeof b, "%d", v + 1);
            else                  snprintf(b, sizeof b, "%d", v);
            cell.value = b;
            cells.push_back(cell);
        }
        patchRow_->setCells(cells);
    }

    if (patchState_) {
        // Which lanes actually strike this stroke, so the page says what it is
        // connected to. A lane on a cartridge voice is marked, because for that
        // lane this patch is not what sounds.
        std::string lanes;
        const char ch = mseq::kStrokeChars[stroke_];
        for (int l = 0; l < mseq::kLanes; l++) {
            const char* pat = plug_->rhythmSteps(l);
            bool strikes = false;
            for (const char* q = pat; q && *q; q++) if (*q == ch) { strikes = true; break; }
            if (!strikes) continue;
            char one[8];
            snprintf(one, sizeof one, "%d%s ", l + 1, plug_->laneHasVoice(l) ? "*" : "");
            lanes += one;
        }
        if (lanes.empty()) lanes = "none ";
        lanes.pop_back();

        char b[224];
        snprintf(b, sizeof b, "OP %d / %s / LANES %s / PAGE %d", op_ + 1,
                 plug_->strokeEdited(stroke_) ? "EDITED" : "DERIVED",
                 lanes.c_str(), patchPage_ + 1);
        patchState_->setText(b);
    }
}

void SeqEditor::refreshSet() {
    if (!setName_ || !plug_) return;
    std::string n = plug_->setPath().empty() ? std::string("init") : plug_->setName();
    if (plug_->setDirty()) n += "  *";
    setName_->setText(n.c_str());
}

void SeqEditor::showKitName() {
    if (kitName_ && plug_)
        kitName_->setText(SeqVST::kitLabel(plug_->kitPath()).c_str());
}

void SeqEditor::showVoiceName() {
    if (!voiceName_ || !plug_) return;
    char b[192];
    if (plug_->laneHasVoice(lane_))
        snprintf(b, sizeof b, "LANE %d  %s", lane_ + 1, plug_->laneVoiceName(lane_));
    else
        snprintf(b, sizeof b, "LANE %d  derived", lane_ + 1);
    voiceName_->setText(b);
}

CMessageResult SeqEditor::notify(CBaseObject* sender, const char* message) {
    if (message == CVSTGUITimer::kMsgTimer && plug_) {
        const mseq::EngineView& v = plug_->snapshot();
        if (grid_) {
            for (int l = 0; l < mseq::kLanes; l++)
                grid_->setLane(l, plug_->rhythmSteps(l), plug_->rhythmName(l),
                               plug_->rhythmMeter(l));
            grid_->setEngine(v);
        }
        if (song_ && page_ == 0) refreshSong();
        if (status_) {
            char buf[192];
            snprintf(buf, sizeof buf, "%s / %s / SEC %d / BAR %d.%d / %d VOICES",
                     v.running ? "PLAYING" : "STOPPED", v.engine ? "DX7" : "FM",
                     v.section + 1, v.barInSection + 1,
                     std::max(1, v.sectionBars), v.voices);
            status_->setText(buf);
        }
    }
    return VSTGUIEditor::notify(sender, message);
}

} // namespace Vst
} // namespace Steinberg
