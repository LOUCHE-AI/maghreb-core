// Maghreb Seq editor.
//
// Three pages behind one header. The sequence page is the point of the
// window: four lanes on four rhythms of different cycle lengths - 16 against
// 36 against 28 - is impossible to hold in your head from four numbers, and
// obvious the moment you see the rows running at different lengths with the
// playhead crossing them.
//
// The look is a deliberate mix of two references. From Fors Pivot: a colour
// per lane carried everywhere, so the parameter row tells you which lane you
// are editing without a label saying so, and no panels or borders anywhere -
// alignment does the work. From Elektron: square trig pads with every fourth
// one lifted, monospaced type for anything that is a name or a number, and a
// single row of eight parameter cells rather than a knob per parameter.
//
// The twenty knobs are gone. One row of eight shows the selected lane.

#pragma once

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/events.h"

#include "seq_dsp.h"
#include "cart_panel.h"
#include "ui_views.h"

#include <vector>
#include <string>
#include <cmath>
#include <map>
#include <functional>

namespace Steinberg {
namespace Vst {

class SeqVST;

// Both of these now live in shared/ui_views.h, so Tubu DX draws the same rows
// rather than a second copy of them.
using ParamRow = mui::ParamRow;
using OpRows = mui::OpRows;

// One colour per lane, used for the edge bar, the struck trigs, the section
// dots and the parameter bars. It is the only thing that says which lane the
// parameter row belongs to, so it has to be the same colour in all four places.
extern const VSTGUI::CColor kLaneColour[mseq::kLanes];

//------------------------------------------------------------------ lane grid
//
// Four rows: a colour edge, the lane number, the rhythm name, its meter, and
// the pattern as trig pads at that lane's own cycle length. Clicking a row
// selects the lane; clicking its coloured edge mutes it.
class LaneGrid : public VSTGUI::CView {
public:
    using Select = std::function<void(int lane)>;
    using Mute = std::function<void(int lane)>;

    LaneGrid(const VSTGUI::CRect& r, VSTGUI::CFontDesc* mono,
             const Select& onSelect, const Mute& onMute)
        : VSTGUI::CView(r), font_(mono), select_(onSelect), mute_(onMute) {}

    void setEngine(const mseq::EngineView& v) { view_ = v; invalid(); }
    void setLane(int lane, const char* steps, const char* name, const char* meter);
    void setSelected(int lane) { if (sel_ != lane) { sel_ = lane; invalid(); } }

    void draw(VSTGUI::CDrawContext* c) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    CLASS_METHODS(LaneGrid, VSTGUI::CView)

private:
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    Select select_;
    Mute mute_;
    mseq::EngineView view_;
    std::string steps_[mseq::kLanes], names_[mseq::kLanes], meters_[mseq::kLanes];
    int sel_ = 0;
};

//----------------------------------------------------------------- song strip
//
// Eight sections: a numeral for how many bars, and four dots for which lanes
// play. Drag the numeral to change the length, click a dot to toggle a lane.
class SongStrip : public VSTGUI::CView {
public:
    using Edit = std::function<void(int param, double deltaNorm, int phase)>;
    using Toggle = std::function<void(int section, int lane)>;

    SongStrip(const VSTGUI::CRect& r, VSTGUI::CFontDesc* mono,
              const Edit& onEdit, const Toggle& onToggle)
        : VSTGUI::CView(r), font_(mono), edit_(onEdit), toggle_(onToggle) {}

    void setState(const int* bars, const bool* lanes, int current);
    void draw(VSTGUI::CDrawContext* c) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
    CLASS_METHODS(SongStrip, VSTGUI::CView)

private:
    VSTGUI::CRect cellOf(int s) const;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    Edit edit_;
    Toggle toggle_;
    int bars_[mseq::kSections] = {0};
    bool lanes_[mseq::kSections * mseq::kLanes] = {false};
    int cur_ = 0;
    int drag_ = -1;                       // which section is being dragged
    VSTGUI::CCoord lastY_ = 0;
};

// Eleven pads: which stroke the page is editing.
class StrokePads : public VSTGUI::CView {
public:
    using Pick = std::function<void(int stroke)>;
    StrokePads(const VSTGUI::CRect& r, VSTGUI::CFontDesc* mono, const Pick& onPick)
        : VSTGUI::CView(r), mono_(mono), pick_(onPick) {}
    void setSelected(int s) { if (sel_ != s) { sel_ = s; invalid(); } }
    void setEdited(const bool* e);
    void draw(VSTGUI::CDrawContext* c) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    CLASS_METHODS(StrokePads, VSTGUI::CView)
private:
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> mono_;
    Pick pick_;
    int sel_ = 0;
    bool edited_[mseq::kNumStrokes] = {false};
};

//---------------------------------------------------------------------- editor
class SeqEditor : public VSTGUIEditor,
                  public VSTGUI::IControlListener,
                  public VSTGUI::IMouseObserver {
public:
    static const int32_t kUiTagBase   = 2000;
    static const int32_t kLoadKitId   = kUiTagBase;
    static const int32_t kClearKitId  = kUiTagBase + 1;
    static const int32_t kClearVoiceId= kUiTagBase + 2;
    static const int32_t kSetPrevId   = kUiTagBase + 3;
    static const int32_t kSetNextId   = kUiTagBase + 4;
    static const int32_t kSetLoadId   = kUiTagBase + 5;
    static const int32_t kSetSaveId   = kUiTagBase + 6;
    static const int32_t kSetInitId   = kUiTagBase + 7;
    static const int32_t kPageSeqId   = kUiTagBase + 8;
    static const int32_t kPagePatchId = kUiTagBase + 9;
    static const int32_t kPageKitId   = kUiTagBase + 10;
    static const int32_t kRevertId    = kUiTagBase + 11;
    static const int32_t kPatchPageId = kUiTagBase + 12;
    // the three ways a pattern leaves this plug-in as something other than
    // its own audio output
    static const int32_t kWriteMidiId = kUiTagBase + 13;
    static const int32_t kRenderLoopId= kUiTagBase + 14;
    static const int32_t kRenderKitId = kUiTagBase + 15;
    // patch bytes are not VST3 parameters, so they get their own id space
    static const int32_t kPatchBase   = 100000;

    SeqEditor(void* controller, SeqVST* plug);

    bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& type) SMTG_OVERRIDE;
    void PLUGIN_API close() SMTG_OVERRIDE;
    void valueChanged(VSTGUI::CControl* c) SMTG_OVERRIDE;
    void beginEdit(int32_t index) SMTG_OVERRIDE;
    void endEdit(int32_t index) SMTG_OVERRIDE;
    VSTGUI::CMessageResult notify(VSTGUI::CBaseObject* sender,
                                  const char* message) SMTG_OVERRIDE;

    void onMouseEntered(VSTGUI::CView* view, VSTGUI::CFrame* frame) SMTG_OVERRIDE;
    void onMouseExited(VSTGUI::CView* view, VSTGUI::CFrame* frame) SMTG_OVERRIDE;
    void onMouseEvent(VSTGUI::MouseEvent& event, VSTGUI::CFrame* frame) SMTG_OVERRIDE {}

    void syncControl(int paramId, double normalized);

private:
    void refreshValues();
    void refreshSong();
    void refreshSet();
    void setHelp(const char* text);
    void attachHelp(VSTGUI::CView* v, int paramId);
    void selectLane(int lane);
    void showPage(int page);
    void nudgeParam(int param, double deltaNorm, int phase);
    void stepSet(int dir);

    VSTGUI::COptionMenu* addMenu(const VSTGUI::CRect& r, int paramId);
    VSTGUI::CTextLabel* makeLabel(const VSTGUI::CRect& r, const char* text,
                                  const VSTGUI::CColor& col,
                                  VSTGUI::CHoriTxtAlign align, bool mono = false);
    void styleButton(VSTGUI::CTextButton* b);
    VSTGUI::CTextButton* addAction(const VSTGUI::CRect& r, int32_t tag,
                                   const char* label, const char* help);
    void showKitName();
    void showVoiceName();
    void refreshPatch();
    void selectStroke(int stroke);
    void selectOp(int op);
    void editPatch(int cellId, double delta);

    int stroke_ = 0;
    int op_ = 0;
    int patchPage_ = 0;

    SeqVST* plug_ = nullptr;
    LaneGrid* grid_ = nullptr;
    SongStrip* song_ = nullptr;
    ParamRow* prow_ = nullptr;
    mdx::CartPanel* browser_ = nullptr;
    OpRows* ops_ = nullptr;
    StrokePads* pads_ = nullptr;
    ParamRow* patchRow_ = nullptr;
    VSTGUI::CTextLabel* patchState_ = nullptr;
    VSTGUI::CTextButton* songBtn_ = nullptr;
    VSTGUI::CTextButton* loopBtn_ = nullptr;

    VSTGUI::CTextLabel* status_ = nullptr;
    VSTGUI::CTextLabel* help_ = nullptr;
    VSTGUI::CTextLabel* setName_ = nullptr;
    VSTGUI::CTextLabel* kitName_ = nullptr;
    VSTGUI::CTextLabel* voiceName_ = nullptr;
    VSTGUI::COptionMenu* rhythmMenu_ = nullptr;
    VSTGUI::COptionMenu* kitMenu_ = nullptr;
    VSTGUI::CTextButton* muteBtn_ = nullptr;
    VSTGUI::CTextButton* tabs_[3] = {nullptr, nullptr, nullptr};

    std::vector<VSTGUI::CView*> pageViews_[3];
    int page_ = 0;
    int lane_ = 0;

    VSTGUI::SharedPointer<VSTGUI::CFontDesc> labelFont_, valueFont_, bigFont_, titleFont_;

    std::vector<VSTGUI::CControl*> bound_;
    std::map<VSTGUI::CView*, int> helpFor_;
};

} // namespace Vst
} // namespace Steinberg
