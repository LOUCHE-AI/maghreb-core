// The cartridge browser: two lists and a search box.
//
// Left is every cartridge in the folder, right is the thirty-two voices in
// the one selected. Type in the box and the right-hand list becomes results
// from across all of them, each labelled with the cartridge it came from, so
// finding a patch by name does not mean opening thirty-four files.
//
// Nothing here knows what a plug-in does with the pick - it hands back a path
// and a voice number and lets the caller decide whether that lands on a pad, a
// tabʿ, or a drum lane.

#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/controls/icontrollistener.h"
#include "vstgui/lib/controls/ctextedit.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/cbuttons.h"
#include "vstgui/lib/controls/coptionmenu.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/cvstguitimer.h"

#include "cart_index.h"
#include "cart_favourites.h"

#include <atomic>
#include <thread>

#include <functional>
#include <atomic>
#include <string>
#include <thread>
#include <vector>

namespace mdx {

// The host editor's colours, so the browser looks like the plug-in it is in
// rather than like a file dialog.
struct BrowserPalette {
    VSTGUI::CColor panel, panelHi, line, text, dim, accent, selBack;
};

// A scrollable list of strings. Deliberately plain: fixed row height, one
// selection, wheel to scroll, no headers of its own.
class ListView : public VSTGUI::CView {
public:
    using Pick = std::function<void(int index)>;

    ListView(const VSTGUI::CRect& r, const BrowserPalette& pal,
             VSTGUI::CFontDesc* font, const Pick& onPick);

    void setRows(const std::vector<std::string>& rows, int selected);
    void setSelected(int i, bool scrollIntoView = true);
    int  selected() const { return sel_; }
    int  rowCount() const { return (int)rows_.size(); }
    void setEmptyText(const std::string& t) { empty_ = t; invalid(); }

    void draw(VSTGUI::CDrawContext* c) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& e) override;
    void onMouseWheelEvent(VSTGUI::MouseWheelEvent& e) override;

    CLASS_METHODS(ListView, VSTGUI::CView)

private:
    static const int kRowH = 17;
    int visibleRows() const;
    void clampTop();

    BrowserPalette pal_;
    VSTGUI::SharedPointer<VSTGUI::CFontDesc> font_;
    Pick pick_;
    std::vector<std::string> rows_;
    std::string empty_ = "nothing here";
    int sel_ = -1;
    int top_ = 0;
};

// The browser proper. Owns the search field and the two lists.
class CartPanel : public VSTGUI::CViewContainer,
                  public VSTGUI::IControlListener {
public:
    // path of the cartridge, voice index within it, and a label already built
    // for a status line
    using Pick = std::function<void(const std::string& path, int voice,
                                    const std::string& label)>;

    CartPanel(const VSTGUI::CRect& r, const BrowserPalette& pal,
              VSTGUI::CFontDesc* labelFont, VSTGUI::CFontDesc* monoFont);

    // Reads the folder, on a worker thread.
    //
    // Safe to call again; the current pick is kept if the cartridge is still
    // there. It returns immediately and the lists fill in when the scan lands,
    // which for a folder on this machine is the same frame - thirteen
    // milliseconds for two thousand cartridges, measured. The reason it is not
    // done in line is where the folder can point: these are symlinked as often
    // as not, and a link onto a network mount turns a directory walk into
    // however long that mount takes to answer. A plug-in window that stops
    // answering its host gets its process killed for hanging.
    void rescan(const std::string& folder);

    // True while a scan is in flight. The lists show what they had before.
    bool scanning() const { return scanning_; }

    void setPick(const Pick& p) { pick_ = p; }

    // Where this plug-in keeps its own starred patches. Per plug-in on
    // purpose: what suits a drum machine is not what suits the ṭubūʿ.
    void setFavouritesFile(const std::string& path);

    // Puts the lists back on a known cartridge and voice, for restoring a Set.
    void showSelection(const std::string& path, int voice);

    int cartCount() const { return index_.count(); }

    void valueChanged(VSTGUI::CControl* c) override;

    ~CartPanel() override;

    // NOCOPY: the panel owns a scan in flight, and a thread is not a thing
    // that can be duplicated behind its own back. Nothing copies views here.
    CLASS_METHODS_NOCOPY(CartPanel, VSTGUI::CViewContainer)

private:
    void pollScan();
    void adoptScan();
    void rebuildCartList();
    void rebuildVoiceList();
    void applySearch();
    void firePick(int cart, int voice);

    CartIndex index_;
    BrowserPalette pal_;
    Pick pick_;

    VSTGUI::CTextEdit* search_ = nullptr;
    ListView* carts_ = nullptr;
    ListView* voices_ = nullptr;
    VSTGUI::CTextLabel* count_ = nullptr;

    // The scan builds a whole second index and swaps it in when it is done, so
    // the worker never touches what the lists are drawing from.
    CartIndex incoming_;
    std::thread worker_;
    std::atomic<bool> scanDone_ {false};
    bool scanning_ = false;
    std::string keepPath_;          // the pick to restore when the scan lands
    int keepVoice_ = 0;
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> pollTimer_;

    // Control tags. 1 is the search field.
    static const int32_t kFavOnlyTag = 2;
    static const int32_t kTypeTag    = 3;
    static const int32_t kStarTag    = 4;

    Favourites favs_;
    bool favOnly_ = false;          // show only what is starred
    int typeFilter_ = -1;           // a VoiceType, or -1 for every kind
    VSTGUI::CTextButton* favBtn_ = nullptr;
    VSTGUI::CTextButton* starBtn_ = nullptr;
    VSTGUI::COptionMenu* typeMenu_ = nullptr;

    std::vector<CartHit> hits_;     // populated only while searching
    bool searching_ = false;
    int curCart_ = -1;
    int curVoice_ = -1;
};

} // namespace mdx
