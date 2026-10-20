// Tubu DX editor.
//
// The ruler is the whole point of the window. A tabʿ is seven numbers in
// cents, and what matters about it is where those numbers fall relative to
// the twelve keys a keyboard has - so the ruler draws both, the degrees
// against the 12-TET grid, and marks the ones that have no key. Reading it
// tells you what the mode is in a way a list of numbers does not, and
// dragging a degree along it is the natural way to retune.
//
// Same visual language as TapeGrain: flat, near-monochrome, one accent,
// monospaced numerals, nothing drawn that is not carrying information.

#pragma once

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cfont.h"

#include "tubudx_dsp.h"
#include "cart_panel.h"
#include "ui_views.h"
#include "cart_panel.h"

#include <vector>
#include <string>
#include <cmath>
#include <functional>
#include <map>

namespace Steinberg {
namespace Vst {

class TubuDXVST;

//------------------------------------------------------------------------
// A thin arc rather than a knob body, growing from an origin so a parameter
// with a meaningful zero reads outward from it.
class RingKnob : public VSTGUI::CKnob {
public:
    RingKnob(const VSTGUI::CRect& r, VSTGUI::IControlListener* l, int32_t tag,
             float origin)
        : VSTGUI::CKnob(r, l, tag, nullptr, nullptr), origin_(origin) {}
    void draw(VSTGUI::CDrawContext* c) override;
    CLASS_METHODS(RingKnob, VSTGUI::CKnob)
private:
    float origin_ = 0.f;
};

//------------------------------------------------------------------------
// The tabʿ, drawn against the twelve keys it does not fit into.
class RulerView : public VSTGUI::CView {
public:
    explicit RulerView(const VSTGUI::CRect& r) : VSTGUI::CView(r) {}

    void setEngine(const tdx::EngineView& v) { view_ = v; invalid(); }
    void setTonicName(const std::string& s) { tonic_ = s; invalid(); }

    // phase: 0 begins a drag, 1 continues it, 2 ends it
    using DragFn = std::function<void(int degree, int cents, int phase)>;
    void setDragHandler(DragFn f) { drag_ = std::move(f); }

    void draw(VSTGUI::CDrawContext* c) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent& e) override;

    CLASS_METHODS(RulerView, VSTGUI::CView)

private:
    VSTGUI::CCoord xForCents(double cents) const;
    int centsForX(VSTGUI::CCoord x) const;
    int degreeNear(VSTGUI::CCoord x) const;

    tdx::EngineView view_;
    std::string tonic_ = "C3";
    int dragging_ = -1;
    DragFn drag_;
};

//------------------------------------------------------------------------
class TubuDXEditor : public VSTGUIEditor,
                   public VSTGUI::IControlListener,
                   public VSTGUI::IMouseObserver {
public:
    static const int32_t kUiTagBase = 900;
    static const int32_t kLoadSyxTag = 905;
    static const int32_t kPageTuneId  = 910;
    static const int32_t kPagePatchId = 911;
    static const int32_t kPageCartId  = 912;
    static const int32_t kPatchRevertId = 913;
    static const int32_t kPatchPageId   = 914;
    // patch bytes are not VST3 parameters, so they get their own id space
    static const int32_t kPatchBase     = 100000;

    TubuDXEditor(void* controller, TubuDXVST* plug);

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
    void setHelp(const char* text);
    void attachHelp(VSTGUI::CView* v, int paramId);
    VSTGUI::CControl* addParamControl(const VSTGUI::CRect& cell, int paramId,
                                      VSTGUI::CViewContainer* into);
    VSTGUI::CTextLabel* makeLabel(const VSTGUI::CRect& r, const char* text,
                                  const VSTGUI::CColor& col,
                                  VSTGUI::CHoriTxtAlign align, bool mono = false);
    void styleButton(VSTGUI::CTextButton* b);

    TubuDXVST* plug_ = nullptr;
    RulerView* ruler_ = nullptr;
    VSTGUI::CTextLabel* status_ = nullptr;
    VSTGUI::CTextLabel* voiceName_ = nullptr;
    mdx::CartPanel* browser_ = nullptr;
    mui::OpRows* ops_ = nullptr;
    mui::ParamRow* prow_ = nullptr;
    mui::ParamRow* patchRow_ = nullptr;
    int op_ = 0;
    int patchPage_ = 0;
    void editPatch(int cellId, double delta);
    VSTGUI::CTextLabel* algoText_ = nullptr;
    VSTGUI::CTextButton* tabs_[3] = {nullptr, nullptr, nullptr};
    std::vector<VSTGUI::CView*> pageViews_[3];
    int page_ = 0;
    void showPage(int page);
    void refreshPatchView();
    VSTGUI::CTextLabel* help_ = nullptr;

    VSTGUI::SharedPointer<VSTGUI::CFontDesc> labelFont_, valueFont_, bigFont_, titleFont_;

    std::vector<VSTGUI::CControl*> bound_;
    std::vector<VSTGUI::CTextLabel*> readouts_;
    std::map<VSTGUI::CView*, int> helpFor_;
};

} // namespace Vst
} // namespace Steinberg
