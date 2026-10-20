// Maghreb Tune editor.
//
// The window has one job: show that the note is being put on a degree of the
// ṭabʿ rather than on a semitone. So the centre is a cents ruler with both
// grids drawn on it - the seven degrees, and the twelve a corrector would
// otherwise snap to - with the sung pitch on it and a line to where it is
// going. When the two grids disagree, you can see it.
//
// Same language as the rest of the family: near-black, one row of parameter
// cells, no panels.

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

#include "tune_dsp.h"
#include "ui_views.h"

#include <vector>
#include <string>
#include <map>
#include <functional>

namespace Steinberg {
namespace Vst {

class TuneVST;

// One octave of cents, with the ṭabʿ on it and twelve-tone under it.
class PitchView : public VSTGUI::CView {
public:
    using DragDegree = std::function<void(int degree, int cents, int phase)>;

    PitchView(const VSTGUI::CRect& r, VSTGUI::CFontDesc* mono, const DragDegree& d)
        : VSTGUI::CView(r), font_(mono), drag_(d) {}

    void setEngine(const mtune::EngineView& v) { view_ = v; invalid(); }
    void setDegrees(const int* cents) {
        for (int i = 0; i < tubu::kDegrees; i++) deg_[i] = cents[i];
        invalid();
    }
    void setTonicName(const std::string& s) { tonic_ = s; invalid(); }

    void draw(VSTGUI::CDrawContext* c) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
    CLASS_METHODS(PitchView, VSTGUI::CView)

private:
    VSTGUI::CCoord xFor(double cents) const;
    int centsFor(VSTGUI::CCoord x) const;
    int degreeNear(VSTGUI::CCoord x) const;

    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    DragDegree drag_;
    mtune::EngineView view_;
    int deg_[tubu::kDegrees] = {0};
    std::string tonic_ = "C3";
    int dragging_ = -1;
};

class TuneEditor : public VSTGUIEditor,
                   public VSTGUI::IControlListener,
                   public VSTGUI::IMouseObserver {
public:
    static const int32_t kUiTagBase = 900;
    static const int32_t kRevertId  = 901;
    static const int32_t kStyleId   = 902;

    TuneEditor(void* controller, TuneVST* plug);

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
    VSTGUI::CTextLabel* makeLabel(const VSTGUI::CRect& r, const char* text,
                                  const VSTGUI::CColor& col,
                                  VSTGUI::CHoriTxtAlign align, bool mono = false);
    void styleButton(VSTGUI::CTextButton* b);

    TuneVST* plug_ = nullptr;
    PitchView* pitch_ = nullptr;
    mui::ParamRow* prow_ = nullptr;
    VSTGUI::CTextLabel* status_ = nullptr;
    VSTGUI::CTextLabel* help_ = nullptr;
    VSTGUI::COptionMenu* tabMenu_ = nullptr;
    VSTGUI::COptionMenu* styleMenu_ = nullptr;
    VSTGUI::CTextButton* bypassBtn_ = nullptr;

    VSTGUI::SharedPointer<VSTGUI::CFontDesc> labelFont_, valueFont_, bigFont_, titleFont_;

    std::vector<VSTGUI::CControl*> bound_;
    std::map<VSTGUI::CView*, int> helpFor_;
};

} // namespace Vst
} // namespace Steinberg
