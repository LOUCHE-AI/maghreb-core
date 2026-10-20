// TapeRepair editor.
//
// The window is mostly the waveform, because the whole point of this device is
// that you can see what you recorded and what the repair did to it. Everything
// else is a row of settings and four things to press.

#pragma once

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/cfont.h"

#include "taperepair_dsp.h"
#include "ui_views.h"

#include <string>
#include <vector>

namespace Steinberg {
namespace Vst {

class TapeRepairVST;

// The take, drawn as min/max per column, with the repair over it and the
// playhead through it. Clicking picks where LEARN measures from - which is the
// one thing about denoising that wants a person to look at the picture.
class WaveView : public VSTGUI::CView {
public:
    using Pick = std::function<void(double fraction)>;

    WaveView(const VSTGUI::CRect& r, const mui::Palette& pal, const Pick& onPick)
        : VSTGUI::CView(r), pal_(pal), pick_(onPick) {}

    void setPeaks(const trep::TapeRepair::Peaks& tape,
                  const trep::TapeRepair::Peaks& fixed) {
        tape_ = tape; fixed_ = fixed; invalid();
    }
    void setPlayhead(double f) { if (std::fabs(f - head_) > 1e-4) { head_ = f; invalid(); } }
    void setLearnAt(double f, double width) { learnAt_ = f; learnW_ = width; invalid(); }

    void draw(VSTGUI::CDrawContext* c) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    CLASS_METHODS(WaveView, VSTGUI::CView)

private:
    mui::Palette pal_;
    Pick pick_;
    trep::TapeRepair::Peaks tape_, fixed_;
    double head_ = -1.0, learnAt_ = -1.0, learnW_ = 0.0;
};

class TapeRepairEditor : public VSTGUIEditor,
                         public VSTGUI::IControlListener,
                         public VSTGUI::IMouseObserver {
public:
    static const int32_t kUiTagBase   = 900;
    static const int32_t kArmTag      = 900;
    static const int32_t kPlayTag     = 901;
    static const int32_t kCompareTag  = 902;
    static const int32_t kLearnTag    = 903;
    static const int32_t kRepairTag   = 904;
    static const int32_t kRevertTag   = 905;
    static const int32_t kSendTag     = 906;
    static const int32_t kLoadTag     = 907;
    static const int32_t kFollowTag   = 908;
    static const int32_t kAutoStopTag = 909;
    static const int32_t kSplitTag    = 910;

    TapeRepairEditor(void* controller, TapeRepairVST* plug);

    bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& type) SMTG_OVERRIDE;
    void PLUGIN_API close() SMTG_OVERRIDE;
    void valueChanged(VSTGUI::CControl* c) SMTG_OVERRIDE;
    void beginEdit(int32_t index) SMTG_OVERRIDE {}
    void endEdit(int32_t index) SMTG_OVERRIDE {}
    VSTGUI::CMessageResult notify(VSTGUI::CBaseObject* sender, const char* message) SMTG_OVERRIDE;

    void onMouseEntered(VSTGUI::CView* view, VSTGUI::CFrame* frame) SMTG_OVERRIDE;
    void onMouseExited(VSTGUI::CView* view, VSTGUI::CFrame* frame) SMTG_OVERRIDE;
    void onMouseEvent(VSTGUI::MouseEvent&, VSTGUI::CFrame*) SMTG_OVERRIDE {}

private:
    VSTGUI::CTextLabel* makeLabel(const VSTGUI::CRect& r, const char* text,
                                  const VSTGUI::CColor& col, int align, bool mono = false);
    void styleButton(VSTGUI::CTextButton* b);
    void setHelp(const char* text);
    void refreshWave();
    void refreshCells();
    void refreshStatus();

    TapeRepairVST* plug_ = nullptr;
    bool wasWorking_ = false;      // so the repair is picked up once, when it lands
    VSTGUI::CTextButton* followBtn_ = nullptr;
    VSTGUI::CTextButton* autoStopBtn_ = nullptr;

    VSTGUI::SharedPointer<VSTGUI::CFontDesc> labelFont_, valueFont_, bigFont_, titleFont_;
    WaveView* wave_ = nullptr;
    mui::ParamRow* row_ = nullptr;
    VSTGUI::CTextLabel* status_ = nullptr;
    VSTGUI::CTextLabel* help_ = nullptr;
    VSTGUI::CTextButton* armBtn_ = nullptr;
    VSTGUI::CTextButton* playBtn_ = nullptr;
    VSTGUI::CTextButton* compareBtn_ = nullptr;

    double learnAt_ = -1.0;      // fraction of the take, or -1 for "find it yourself"
    int ticks_ = 0;
};

} // namespace Vst
} // namespace Steinberg
