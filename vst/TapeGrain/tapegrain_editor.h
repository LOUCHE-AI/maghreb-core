// TapeGrain editor.
//
// Built in code rather than from a .uidesc so the plug-in stays a single
// binary with no resource files to lose.
//
// The layout follows the module's own menu hierarchy - Main, Shape, Motion,
// Pads, Sync - because that is the split the parameters were designed
// around, and it keeps thirty-four controls from arriving as one wall. The
// waveform, transport and preset bar stay above the sections, since they are
// context for every page rather than part of any one.
//
// Visually it is deliberately flat and near-monochrome: one accent, thin
// geometry, monospaced numbers, and no chrome that is not carrying
// information. Where a setting can be shown by changing the data itself
// rather than by drawing a box over it, it is - Span brightens the audio
// inside it instead of tinting a rectangle on top.
//
// Control tags are the real parameter ids, which is what makes the host's
// automation and touch handling work without any translation layer. Tags at
// or above kUiTagBase are chrome and are kept away from the host.

#pragma once

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/cfileselector.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cfont.h"

#include "tapegrain_dsp.h"
#include "tapegrain_separate.h"
#include "ui_views.h"

#include <vector>
#include <string>
#include <cmath>
#include <functional>
#include <map>

namespace Steinberg {
namespace Vst {

class TapeGrainVST;

//------------------------------------------------------------------------
// A thin arc rather than a knob body. The arc grows from an origin, so a
// bipolar parameter reads outward from its centre and Output reads outward
// from unity gain - which is where the eye expects "no change" to be.
class RingKnob : public VSTGUI::CKnob {
public:
    RingKnob(const VSTGUI::CRect& r, VSTGUI::IControlListener* l, int32_t tag,
             float origin)
        : VSTGUI::CKnob(r, l, tag, nullptr, nullptr), origin_(origin) {}

    void draw(VSTGUI::CDrawContext* c) override;
    CLASS_METHODS(RingKnob, VSTGUI::CKnob)

private:
    float origin_ = 0.f;   // normalized position the value arc grows from
};

//------------------------------------------------------------------------
// Draws whatever is loaded, with the detected slices, the swarm and the live
// grains on top. It keeps its own decimated copy: the buffer belongs to the
// audio thread.
class WaveformView : public VSTGUI::CView {
public:
    explicit WaveformView(const VSTGUI::CRect& r) : VSTGUI::CView(r) {}

    void setOutline(std::vector<float>&& peaks) {
        peaks_ = std::move(peaks);
        invalid();
    }
    void setSlices(std::vector<double>&& s) { slices_ = std::move(s); invalid(); }
    void setPlaceholder(const std::string& s) { placeholder_ = s; invalid(); }

    // The live layer: agents, grains and playheads. Redrawn only while
    // something is sounding, so a silent instrument costs nothing.
    void setEngine(const tg::EngineView& v, bool sounding) {
        view_ = v;
        sounding_ = sounding;
        invalid();
    }
    void clearEngine() {
        if (!sounding_) return;
        sounding_ = false;
        view_.numGrains = 0;
        invalid();
    }

    // Position and Span. The motion modes work over an area, not a point, so
    // showing only the point hides half of what the settings mean.
    void setRegion(float pos, float span) {
        if (std::fabs(pos - pos_) < 1e-4f && std::fabs(span - span_) < 1e-4f) return;
        pos_ = pos; span_ = span;
        invalid();
    }

    // phase: 0 begins a drag, 1 continues it, 2 ends it
    using DragFn = std::function<void(float pos, float span, int phase)>;
    void setDragHandler(DragFn f) { drag_ = std::move(f); }

    void draw(VSTGUI::CDrawContext* c) override;

    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& e) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& e) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent& e) override;

    CLASS_METHODS(WaveformView, VSTGUI::CView)

private:
    enum DragMode { dragNone = 0, dragPos, dragLeft, dragRight };
    float xToNorm(VSTGUI::CCoord x) const;
    void applyDrag(VSTGUI::CCoord x);

    std::vector<float> peaks_;
    std::vector<double> slices_;
    tg::EngineView view_;
    bool sounding_ = false;
    std::string placeholder_;

    float pos_ = 0.25f, span_ = 0.25f;
    DragMode mode_ = dragNone;
    float grabOffset_ = 0.f;
    DragFn drag_;
};

//------------------------------------------------------------------------
class TapeGrainEditor : public VSTGUIEditor,
                        public VSTGUI::IControlListener,
                        public VSTGUI::IMouseObserver {
public:
    static const int32_t kUiTagBase   = 900;   // above every parameter id
    static const int32_t kLoadTag      = 900;
    static const int32_t kSavePreTag   = 901;
    static const int32_t kLoadPreTag   = 902;
    static const int32_t kLoadStemsTag = 903;
    static const int32_t kSeparateTag  = 904;
    static const int32_t kFromTapeTag  = 905;
    static const int32_t kTabTag      = 910;   // .. kTabTag + kNumSections - 1
    static const int32_t kStemTag      = 920;  // .. kStemTag + kStemSlots - 1
    static const int32_t kStemAllTag   = 929;
    static const int32_t kStemOnTag    = 930;  // .. kStemOnTag + kStemSlots - 1

    TapeGrainEditor(void* controller, TapeGrainVST* plug);

    bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& type) SMTG_OVERRIDE;
    void PLUGIN_API close() SMTG_OVERRIDE;

    void valueChanged(VSTGUI::CControl* c) SMTG_OVERRIDE;

    // chrome is not a parameter, so the host must not hear about a touch on it
    void beginEdit(int32_t index) SMTG_OVERRIDE;
    void endEdit(int32_t index) SMTG_OVERRIDE;

    VSTGUI::CMessageResult notify(VSTGUI::CBaseObject* sender,
                                  const char* message) SMTG_OVERRIDE;

    // IMouseObserver: the help line follows whatever is under the cursor,
    // which is quicker to read than a tooltip you have to wait for
    void onMouseEntered(VSTGUI::CView* view, VSTGUI::CFrame* frame) SMTG_OVERRIDE;
    void onMouseExited(VSTGUI::CView* view, VSTGUI::CFrame* frame) SMTG_OVERRIDE;
    void onMouseEvent(VSTGUI::MouseEvent& event, VSTGUI::CFrame* frame) SMTG_OVERRIDE {}

    // called by the plug-in when automation or a preset moves a parameter
    void syncControl(int paramId, double normalized);
    // called by the plug-in when the buffer has been replaced
    void refreshWaveform();

private:
    void showSection(int index);
    void refreshValues();
    void refreshRegion();      // pull Position/Span back onto the waveform
    void refreshStems();       // light the active slot, grey the empty ones
    void setHelp(const char* text);
    void attachHelp(VSTGUI::CView* v, int paramId);
    VSTGUI::CControl* addParamControl(const VSTGUI::CRect& cell, int paramId);
    VSTGUI::CTextLabel* makeLabel(const VSTGUI::CRect& r, const char* text,
                                  const VSTGUI::CColor& col,
                                  VSTGUI::CHoriTxtAlign align, bool mono = false);
    void styleButton(VSTGUI::CTextButton* b);

    TapeGrainVST* plug_ = nullptr;
    WaveformView* wave_ = nullptr;
    mui::ParamRow* prow_ = nullptr;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> bigFont_;
    VSTGUI::CTextLabel* status_ = nullptr;
    VSTGUI::CTextLabel* outs_ = nullptr;

    // Demucs runs as a child process; the idle tick moves it along.
    tg::Separator sep_;
    VSTGUI::CTextLabel* voices_ = nullptr;
    VSTGUI::CTextLabel* help_ = nullptr;
    std::vector<VSTGUI::CTextLabel*> tabMarks_;  // accent bar, one per tab
    VSTGUI::CViewContainer* page_ = nullptr;

    VSTGUI::SharedPointer<VSTGUI::CFontDesc> labelFont_, valueFont_, titleFont_;

    std::vector<VSTGUI::CControl*> tabs_;
    std::vector<VSTGUI::CControl*> stemBtns_;      // the on/off lights
    std::vector<VSTGUI::CControl*> stemSelBtns_;   // the letters, which select
    VSTGUI::CControl* allBtn_ = nullptr;
    std::vector<VSTGUI::CControl*> bound_;      // tag == parameter id
    std::vector<VSTGUI::CTextLabel*> readouts_; // one per bound_, same order
    // every view that should explain itself, including the labels around a
    // control - hovering the word "Agents" should say as much as the knob
    std::map<VSTGUI::CView*, int> helpFor_;
    int section_ = 0;
    int ticks_ = 0;
};

} // namespace Vst
} // namespace Steinberg
