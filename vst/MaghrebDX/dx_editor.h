// Maghreb DX editor.
//
// The pad row is the point of the window: eleven strokes laid out as they are
// on the Move, each lighting as it sounds. Clicking one selects it for editing
// and strikes it, because on a drum synth those two things always want to
// happen together - you cannot judge an edit you cannot hear.

#pragma once

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cfont.h"

#include "dx_dsp.h"
#include "ui_views.h"
#include "cart_panel.h"

#include <vector>
#include <string>
#include <cmath>
#include <functional>
#include <map>

namespace Steinberg {
namespace Vst {

class DXVST;

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

// Eleven pads: the kit, lighting as it plays.
class PadRow : public VSTGUI::CView {
public:
    explicit PadRow(const VSTGUI::CRect& r) : VSTGUI::CView(r) {}

    void setEngine(const mdx::EngineView& v) { view_ = v; invalid(); }
    void setEdited(int i, bool e) {
        if (i < 0 || i >= mdx::kNumStrokes || edited_[i] == e) return;
        edited_[i] = e;
        invalid();
    }
    void setHandler(std::function<void(int)> f) { hit_ = std::move(f); }

    void draw(VSTGUI::CDrawContext* c) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    CLASS_METHODS(PadRow, VSTGUI::CView)

private:
    int padAt(VSTGUI::CCoord x) const;

    mdx::EngineView view_;
    bool edited_[mdx::kNumStrokes] = {false};
    std::function<void(int)> hit_;
};

class DXEditor : public VSTGUIEditor,
                 public VSTGUI::IControlListener,
                 public VSTGUI::IMouseObserver {
public:
    static const int32_t kUiTagBase = 2000;
    static const int32_t kGroupTag = 2020;   // six, one per group tab
    static const int32_t kSaveKitTag = 2010;
    static const int32_t kLoadKitTag = 2011;
    static const int32_t kLoadSyxTag = 2012;
    static const int32_t kVoiceUpTag = 2013;
    static const int32_t kVoiceDnTag = 2014;
    static const int32_t kAssignTag  = 2015;
    static const int32_t kDerivedTag = 2016;

    DXEditor(void* controller, DXVST* plug);

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
    void addCell(int x, int y, int paramId);
    VSTGUI::CTextLabel* makeLabel(const VSTGUI::CRect& r, const char* text,
                                  const VSTGUI::CColor& col,
                                  VSTGUI::CHoriTxtAlign align, bool mono = false);
    void styleButton(VSTGUI::CTextButton* b);

    DXVST* plug_ = nullptr;
    PadRow* pads_ = nullptr;
    VSTGUI::CTextLabel* status_ = nullptr;
    VSTGUI::CTextLabel* strokeName_ = nullptr;
    VSTGUI::CTextLabel* voiceName_ = nullptr;
    mdx::CartPanel* browser_ = nullptr;
    VSTGUI::CTextLabel* help_ = nullptr;
    mui::ParamRow* prow_ = nullptr;
    std::vector<VSTGUI::CTextButton*> groupTabs_;
    int group_ = 0;
    void showGroup(int g);
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> bigFont_;

    VSTGUI::SharedPointer<VSTGUI::CFontDesc> labelFont_, valueFont_, titleFont_;

    std::vector<VSTGUI::CControl*> bound_;
    std::vector<VSTGUI::CTextLabel*> readouts_;
    std::vector<int> readoutIds_;
    std::map<VSTGUI::CView*, int> helpFor_;
};

} // namespace Vst
} // namespace Steinberg
