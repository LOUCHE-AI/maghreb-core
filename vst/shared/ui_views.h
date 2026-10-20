// The two views both editors need.
//
// Maghreb Seq and Tubu DX show the same two things: a row of eight parameter
// cells you drag, and a stack of FM operator rows with their wiring drawn down
// the left. They were written for Maghreb Seq first; rather than copy them
// into Tubu DX - the three vendored msfa folders are already lesson enough
// about that - they live here and both include them.
//
// Nothing here knows what a parameter is. Cells carry an opaque id and a
// callback; whether it turns into a VST3 parameter or a raw patch byte is the
// editor's business.

#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/events.h"

#include <functional>
#include <string>
#include <vector>

namespace mui {

// The host editor's colours, so these views look like the plug-in they are in.
struct Palette {
    VSTGUI::CColor bg, cell, sel, rule, ink, lab, dim;
};

inline VSTGUI::CColor dim(const VSTGUI::CColor& c, float f) {
    return VSTGUI::CColor((uint8_t)(c.red * f), (uint8_t)(c.green * f),
                          (uint8_t)(c.blue * f), c.alpha);
}

//--------------------------------------------------------------- parameter row
//
// Eight cells: a small label, the value, and a bar. Drag a cell vertically to
// change it. This is what replaced the strips of knobs - the same controls for
// whatever is selected, in a fifth of the space.
class ParamRow : public VSTGUI::CView {
public:
    struct Cell {
        int param = -1;
        std::string label, value;
        float norm = 0.f;
        VSTGUI::CColor bar;
    };
    // phase: 0 begins a drag, 1 moves, 2 ends
    using Edit = std::function<void(int param, double deltaNorm, int phase)>;

    ParamRow(const VSTGUI::CRect& r, const Palette& pal, VSTGUI::CFontDesc* small,
             VSTGUI::CFontDesc* big, const Edit& onEdit)
        : VSTGUI::CView(r), pal_(pal), small_(small), big_(big), edit_(onEdit) {}

    void setCells(const std::vector<Cell>& cells) { cells_ = cells; invalid(); }
    int hitCell(VSTGUI::CCoord x) const;

    void draw(VSTGUI::CDrawContext* c) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
    CLASS_METHODS(ParamRow, VSTGUI::CView)

private:
    Palette pal_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> small_, big_;
    Edit edit_;
    std::vector<Cell> cells_;
    int drag_ = -1;
    VSTGUI::CCoord lastY_ = 0;
};

//------------------------------------------------------------- operator rows
//
// One row per FM operator: its wiring, its ratio, its own output waveform, a
// ring for level, a diamond for feedback, and its envelope as a filled shape
// with handles. The left of the view is a routing gutter - a node per
// operator, lines to whatever each one modulates, and an output rail that
// carriers join. The nodes sit at their own row's centre, which is the whole
// reason the wiring is drawn here rather than as a separate diagram.
class OpRows : public VSTGUI::CView {
public:
    struct Op {
        VSTGUI::CColor colour;
        bool carrier = false, on = true, fb = false;
        int target = -1;          // the row this operator modulates, -1 = carrier
        std::string ratio;
        float wave[192] = {0};
        bool haveWave = false;
        float lev[4] = {0.9f, 0.5f, 0.5f, 0.f};   // L1..L4, normalised
        float rt[4]  = {0.9f, 0.6f, 0.4f, 0.5f};  // R1..R4, normalised
    };
    using Select = std::function<void(int op)>;

    OpRows(const VSTGUI::CRect& r, const Palette& pal, VSTGUI::CFontDesc* mono,
           VSTGUI::CFontDesc* big, const Select& onSelect)
        : VSTGUI::CView(r), pal_(pal), mono_(mono), big_(big), select_(onSelect) {}

    void setOps(const std::vector<Op>& ops) { ops_ = ops; invalid(); }
    void setSelected(int op) { if (sel_ != op) { sel_ = op; invalid(); } }

    void draw(VSTGUI::CDrawContext* c) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    CLASS_METHODS(OpRows, VSTGUI::CView)

private:
    void drawRouting(VSTGUI::CDrawContext* c);

    Palette pal_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> mono_, big_;
    Select select_;
    std::vector<Op> ops_;
    int sel_ = 0;
};

} // namespace mui
