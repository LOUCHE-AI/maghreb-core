#include "cart_panel.h"

#include <algorithm>
#include <cstdio>

using namespace VSTGUI;

namespace mdx {

//------------------------------------------------------------------ ListView

ListView::ListView(const CRect& r, const BrowserPalette& pal, CFontDesc* font,
                   const Pick& onPick)
    : CView(r), pal_(pal), font_(font), pick_(onPick) {
    setWantsFocus(false);
}

int ListView::visibleRows() const {
    return std::max(1, (int)(getViewSize().getHeight() / kRowH));
}

void ListView::clampTop() {
    const int maxTop = std::max(0, (int)rows_.size() - visibleRows());
    top_ = std::max(0, std::min(top_, maxTop));
}

void ListView::setRows(const std::vector<std::string>& rows, int selected) {
    rows_ = rows;
    sel_ = (selected >= 0 && selected < (int)rows_.size()) ? selected : -1;
    top_ = 0;
    if (sel_ >= 0) setSelected(sel_, true);
    clampTop();
    invalid();
}

void ListView::setSelected(int i, bool scrollIntoView) {
    sel_ = (i >= 0 && i < (int)rows_.size()) ? i : -1;
    if (scrollIntoView && sel_ >= 0) {
        const int n = visibleRows();
        if (sel_ < top_) top_ = sel_;
        else if (sel_ >= top_ + n) top_ = sel_ - n + 1;
        clampTop();
    }
    invalid();
}

void ListView::draw(CDrawContext* c) {
    const CRect r = getViewSize();
    c->setFillColor(pal_.panel);
    c->drawRect(r, kDrawFilled);
    c->setFont(font_);
    c->setDrawMode(kAntiAliasing);

    if (rows_.empty()) {
        c->setFontColor(pal_.dim);
        c->drawString(empty_.c_str(),
                      CRect(r.left + 8, r.top + 6, r.right - 8, r.top + 6 + kRowH),
                      kLeftText);
        c->setFrameColor(pal_.line);
        c->drawRect(r, kDrawStroked);
        return;
    }

    const int n = visibleRows();
    for (int i = 0; i < n && top_ + i < (int)rows_.size(); i++) {
        const int idx = top_ + i;
        CRect row(r.left + 1, r.top + i * kRowH, r.right - 1, r.top + (i + 1) * kRowH);
        if (row.bottom > r.bottom) break;

        if (idx == sel_) {
            c->setFillColor(pal_.selBack);
            c->drawRect(row, kDrawFilled);
        }
        c->setFontColor(idx == sel_ ? pal_.accent : pal_.text);
        CRect t = row;
        t.left += 7; t.right -= 6; t.top += 1;
        c->drawString(rows_[idx].c_str(), t, kLeftText);
    }

    // A scrollbar only when there is something to scroll, drawn as a hairline
    // so it reads as position rather than as a control.
    if ((int)rows_.size() > n) {
        const double frac = (double)n / (double)rows_.size();
        const double off = (double)top_ / (double)rows_.size();
        CRect bar(r.right - 4, r.top + r.getHeight() * off,
                  r.right - 2, r.top + r.getHeight() * (off + frac));
        c->setFillColor(pal_.dim);
        c->drawRect(bar, kDrawFilled);
    }

    c->setFrameColor(pal_.line);
    c->drawRect(r, kDrawStroked);
}

void ListView::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft() || rows_.empty()) return;
    const CRect r = getViewSize();
    const int i = top_ + (int)((e.mousePosition.y - r.top) / kRowH);
    if (i < 0 || i >= (int)rows_.size()) return;
    setSelected(i, false);
    if (pick_) pick_(i);
    e.consumed = true;
}

void ListView::onMouseWheelEvent(MouseWheelEvent& e) {
    if (rows_.empty()) return;
    // three rows a notch, and the sign follows the platform's own direction
    const int step = (e.deltaY > 0) ? -3 : (e.deltaY < 0 ? 3 : 0);
    if (step == 0) return;
    top_ += step;
    clampTop();
    invalid();
    e.consumed = true;
}

//----------------------------------------------------------------- CartPanel

static const int kSearchH = 20;
static const int kGap = 6;

CartPanel::CartPanel(const CRect& r, const BrowserPalette& pal,
                     CFontDesc* labelFont, CFontDesc* monoFont)
    : CViewContainer(r), pal_(pal) {
    setBackgroundColor(pal.panel);

    const CCoord W = r.getWidth();
    const CCoord H = r.getHeight();
    const CCoord cartW = std::max<CCoord>(140, W * 0.36);

    search_ = new CTextEdit(CRect(0, 0, cartW, kSearchH), this, 1, "");
    search_->setImmediateTextChange(true);        // filter as you type
    search_->setBackColor(pal.panelHi);
    search_->setFrameColor(pal.line);
    search_->setFontColor(pal.text);
    search_->setFont(labelFont);
    search_->setHoriAlign(kLeftText);
    search_->setPlaceholderString("search patches");
    search_->setTooltipText("Type part of a patch or cartridge name. The "
                            "right-hand list becomes matches from every "
                            "cartridge at once. Clear it to go back to one.");
    addView(search_);

    // The right-hand header: what kind of sound, whether to show only the
    // starred ones, and a button to star what is picked. Then the count, in
    // whatever room is left.
    const CCoord typeW = 84, favW = 46, starW = 40;
    const CCoord right = cartW + kGap;

    typeMenu_ = new COptionMenu(CRect(right, 0, right + typeW, kSearchH), this, kTypeTag);
    typeMenu_->addEntry("Any kind");
    for (int i = 0; i < (int)VoiceType::kCount; i++)
        typeMenu_->addEntry(voiceTypeName((VoiceType)i));
    typeMenu_->setBackColor(pal.panelHi);
    typeMenu_->setFrameColor(pal.line);
    typeMenu_->setFontColor(pal.text);
    typeMenu_->setFont(labelFont);
    typeMenu_->setHoriAlign(kLeftText);
    typeMenu_->setValue(0.f);
    typeMenu_->setTooltipText("Narrow the list to one kind of sound. The kind "
                              "comes from the patch itself - how fast it "
                              "arrives and whether it holds - and from the "
                              "name where the name says something.");
    addView(typeMenu_);

    favBtn_ = new CTextButton(CRect(right + typeW + 4, 0, right + typeW + 4 + favW, kSearchH),
                              this, kFavOnlyTag, "FAVS", CTextButton::kOnOffStyle);
    favBtn_->setFont(labelFont);
    favBtn_->setTooltipText("Show only the patches you have starred, from every "
                            "cartridge at once.");
    addView(favBtn_);

    starBtn_ = new CTextButton(CRect(right + typeW + 8 + favW, 0,
                                     right + typeW + 8 + favW + starW, kSearchH),
                               this, kStarTag, "+", CTextButton::kKickStyle);
    starBtn_->setFont(labelFont);
    starBtn_->setTooltipText("Star the patch that is selected, or unstar it if "
                             "it is already in the pool.");
    addView(starBtn_);

    count_ = new CTextLabel(CRect(right + typeW + 12 + favW + starW, 0, W, kSearchH), "");
    count_->setBackColor(kTransparentCColor);
    count_->setFrameColor(kTransparentCColor);
    count_->setFontColor(pal.dim);
    count_->setFont(labelFont);
    count_->setHoriAlign(kLeftText);
    count_->setMouseEnabled(false);
    addView(count_);

    const CCoord listTop = kSearchH + kGap;
    carts_ = new ListView(CRect(0, listTop, cartW, H), pal, labelFont,
                          [this](int i) {
                              // picking a cartridge leaves the search behind,
                              // because you have just said which one you want
                              if (searching_) {
                                  search_->setText("");
                                  searching_ = false;
                                  hits_.clear();
                              }
                              curCart_ = i;
                              curVoice_ = 0;
                              rebuildVoiceList();
                              firePick(curCart_, curVoice_);
                          });
    carts_->setEmptyText("no cartridges found");
    addView(carts_);

    voices_ = new ListView(CRect(cartW + kGap, listTop, W, H), pal, monoFont,
                           [this](int i) {
                               if (searching_) {
                                   if (i < 0 || i >= (int)hits_.size()) return;
                                   curCart_ = hits_[i].cart;
                                   curVoice_ = hits_[i].voice;
                                   carts_->setSelected(curCart_);
                               } else {
                                   curVoice_ = i;
                               }
                               firePick(curCart_, curVoice_);
                           });
    voices_->setEmptyText("no patches");
    addView(voices_);
}

CartPanel::~CartPanel() {
    // The worker is writing into a member of this object, so it has to be
    // finished before the object goes. There is nothing to cancel - a
    // directory walk has no seam to stop at - so this waits.
    if (worker_.joinable()) worker_.join();
}

void CartPanel::rescan(const std::string& folder) {
    if (scanning_) return;              // one walk of a folder is enough

    keepPath_ = (curCart_ >= 0 && curCart_ < index_.count())
                    ? index_.cart(curCart_).path : std::string();
    keepVoice_ = curVoice_;

    if (worker_.joinable()) worker_.join();
    scanDone_.store(false, std::memory_order_release);
    scanning_ = true;
    if (count_) count_->setText("reading the cartridge folder...");

    worker_ = std::thread([this, folder] {
        incoming_.scan(folder);
        scanDone_.store(true, std::memory_order_release);
    });

    // Fast enough on a local folder that this fires once and finds it done;
    // slow enough on a mount that the window stays alive while it waits.
    pollTimer_ = VSTGUI::owned(new VSTGUI::CVSTGUITimer(
        [this](VSTGUI::CVSTGUITimer*) { pollScan(); }, 40, true));
}

void CartPanel::pollScan() {
    if (!scanDone_.load(std::memory_order_acquire)) return;
    if (pollTimer_) { pollTimer_->stop(); pollTimer_ = nullptr; }
    adoptScan();
}

void CartPanel::adoptScan() {
    if (worker_.joinable()) worker_.join();
    scanning_ = false;

    index_ = std::move(incoming_);
    incoming_ = CartIndex{};

    curCart_ = keepPath_.empty() ? (index_.empty() ? -1 : 0)
                                 : index_.indexOfPath(keepPath_);
    if (curCart_ < 0) curCart_ = index_.empty() ? -1 : 0;
    curVoice_ = std::max(0, std::min(keepVoice_, index_.voiceCount(curCart_) - 1));

    rebuildCartList();
    rebuildVoiceList();
}

void CartPanel::rebuildCartList() {
    std::vector<std::string> rows;
    for (int i = 0; i < index_.count(); i++) rows.push_back(index_.cart(i).name);
    carts_->setRows(rows, curCart_);

    char b[96];
    if (index_.empty())
        snprintf(b, sizeof b, "no .syx in the cartridge folder");
    else
        snprintf(b, sizeof b, "%d cartridges, %d patches", index_.count(),
                 index_.count() * 32);
    count_->setText(b);
}

void CartPanel::rebuildVoiceList() {
    std::vector<std::string> rows;
    // A starred patch is marked in the list rather than hidden away in the
    // filter, so the pool is visible while browsing everything else.
    auto mark = [&](bool starred, const std::string& text) {
        return (starred ? std::string("* ") : std::string("  ")) + text;
    };

    if (searching_) {
        for (const CartHit& h : hits_)
            rows.push_back(mark(favs_.contains(index_.cart(h.cart).path, h.voice),
                                index_.label(h)));
        int sel = -1;
        for (int i = 0; i < (int)hits_.size(); i++)
            if (hits_[i].cart == curCart_ && hits_[i].voice == curVoice_) { sel = i; break; }
        voices_->setRows(rows, sel);
        char b[96];
        snprintf(b, sizeof b, "%d match%s", (int)hits_.size(),
                 hits_.size() == 1 ? "" : "es");
        count_->setText(b);
        return;
    }

    if (curCart_ >= 0 && curCart_ < index_.count()) {
        const CartEntry& c = index_.cart(curCart_);
        for (int i = 0; i < (int)c.voices.size(); i++) {
            char b[64];
            snprintf(b, sizeof b, "%2d  %s", i + 1, c.voices[i].c_str());
            rows.push_back(mark(favs_.contains(c.path, i), b));
        }
    }
    voices_->setRows(rows, curVoice_);
    rebuildCartList();
}

void CartPanel::setFavouritesFile(const std::string& path) {
    favs_.setFile(path);
    rebuildVoiceList();
}

// One list, narrowed by any of three things: what was typed, whether only the
// starred ones are wanted, and which kind of sound. They stack rather than
// replace each other - "starred basses" is a question worth being able to ask.
void CartPanel::applySearch() {
    std::string q = search_ ? std::string(search_->getText().getString()) : std::string();
    // trim, so a stray space does not empty the list
    while (!q.empty() && q.front() == ' ') q.erase(q.begin());
    while (!q.empty() && q.back() == ' ') q.pop_back();

    hits_.clear();
    if (favOnly_) {
        for (const auto& f : favs_.all()) {
            const int c = index_.indexOfPath(f.first);
            if (c >= 0 && f.second >= 0 && f.second < index_.voiceCount(c))
                hits_.push_back({c, f.second});
        }
        if (!q.empty()) {
            const std::string lq = lowered(q);
            hits_.erase(std::remove_if(hits_.begin(), hits_.end(), [&](const CartHit& h) {
                return lowered(index_.cart(h.cart).voices[(size_t)h.voice]).find(lq)
                           == std::string::npos;
            }), hits_.end());
        }
    } else if (!q.empty()) {
        hits_ = index_.find(q);
    } else if (typeFilter_ >= 0) {
        // Nothing else narrowing it, so the kind alone gathers from everywhere.
        for (int c = 0; c < index_.count(); c++)
            for (int v = 0; v < index_.voiceCount(c); v++)
                hits_.push_back({c, v});
    }

    if (typeFilter_ >= 0)
        hits_.erase(std::remove_if(hits_.begin(), hits_.end(), [&](const CartHit& h) {
            return (int)index_.typeOf(h.cart, h.voice) != typeFilter_;
        }), hits_.end());

    searching_ = favOnly_ || !q.empty() || typeFilter_ >= 0;
    rebuildVoiceList();
}

void CartPanel::valueChanged(CControl* c) {
    if (!c) return;
    if (c == search_) { applySearch(); return; }

    switch (c->getTag()) {
        case kFavOnlyTag:
            favOnly_ = c->getValue() >= 0.5f;
            applySearch();
            return;
        case kTypeTag:
            // entry 0 is "Any kind", so the kinds start at 1
            typeFilter_ = (int)std::lround(c->getValue()) - 1;
            applySearch();
            return;
        case kStarTag: {
            if (c->getValue() < 0.5f) return;          // a kick reports twice
            if (curCart_ < 0 || curCart_ >= index_.count()) return;
            favs_.toggle(index_.cart(curCart_).path, curVoice_);
            applySearch();
            return;
        }
        default: return;
    }
}

void CartPanel::showSelection(const std::string& path, int voice) {
    const int i = index_.indexOfPath(path);
    if (i < 0) return;
    curCart_ = i;
    curVoice_ = std::max(0, std::min(voice, index_.voiceCount(i) - 1));
    if (searching_) { search_->setText(""); searching_ = false; hits_.clear(); }
    carts_->setSelected(curCart_);
    rebuildVoiceList();
}

void CartPanel::firePick(int cart, int voice) {
    if (!pick_ || cart < 0 || cart >= index_.count()) return;
    const CartEntry& c = index_.cart(cart);
    if (voice < 0 || voice >= (int)c.voices.size()) return;
    pick_(c.path, voice, c.name + "  " + c.voices[voice]);
}

} // namespace mdx
