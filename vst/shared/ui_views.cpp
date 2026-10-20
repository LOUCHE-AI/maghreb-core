#include "ui_views.h"

#include <algorithm>
#include <cstdio>

using namespace VSTGUI;

namespace mui {

//-------------------------------------------------------------- parameter row
int ParamRow::hitCell(CCoord x) const {
    if (cells_.empty()) return -1;
    const CRect r = getViewSize();
    const CCoord w = r.getWidth() / (CCoord)cells_.size();
    const int i = (int)((x - r.left) / w);
    return (i < 0 || i >= (int)cells_.size()) ? -1 : i;
}

void ParamRow::draw(CDrawContext* c) {
    if (cells_.empty()) return;
    const CRect r = getViewSize();
    const CCoord w = r.getWidth() / (CCoord)cells_.size();
    c->setDrawMode(kAntiAliasing);

    for (size_t i = 0; i < cells_.size(); i++) {
        const CCoord x = r.left + (CCoord)i * w;
        if (i > 0) {
            c->setFrameColor(pal_.rule);
            c->drawLine(CPoint(x, r.top), CPoint(x, r.bottom));
        }
        const CCoord tx = x + (i ? 11 : 0);

        c->setFont(small_);
        c->setFontColor(pal_.dim);
        c->drawString(cells_[i].label.c_str(),
                      CRect(tx, r.top, x + w - 6, r.top + 12), kLeftText);

        c->setFont(big_);
        c->setFontColor(pal_.ink);
        c->drawString(cells_[i].value.c_str(),
                      CRect(tx, r.top + 13, x + w - 6, r.top + 35), kLeftText);

        const CCoord by = r.top + 43;
        c->setFillColor(pal_.cell);
        c->drawRect(CRect(tx, by, x + w - 11, by + 2), kDrawFilled);
        const CCoord full = (x + w - 11) - tx;
        const CCoord fw = full * std::max(0.f, std::min(1.f, cells_[i].norm));
        if (fw > 0) {
            c->setFillColor(cells_[i].bar);
            c->drawRect(CRect(tx, by, tx + fw, by + 2), kDrawFilled);
        }
    }
}

void ParamRow::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft()) return;
    const int i = hitCell(e.mousePosition.x);
    if (i < 0 || cells_[i].param < 0) return;
    drag_ = i;
    lastY_ = e.mousePosition.y;
    if (edit_) edit_(cells_[i].param, 0.0, 0);
    e.consumed = true;
}

void ParamRow::onMouseMoveEvent(MouseMoveEvent& e) {
    if (drag_ < 0 || !edit_) return;
    const double d = (lastY_ - e.mousePosition.y) / 180.0;
    lastY_ = e.mousePosition.y;
    edit_(cells_[drag_].param, d, 1);
    e.consumed = true;
}

void ParamRow::onMouseUpEvent(MouseUpEvent& e) {
    if (drag_ < 0) return;
    if (edit_) edit_(cells_[drag_].param, 0.0, 2);
    drag_ = -1;
    e.consumed = true;
}

//------------------------------------------------------------- operator rows
static const CCoord kRailX = 6, kChanX = 16, kNodeX = 48, kNodeW = 32;
static const CCoord kGutter = 92;
static const CCoord kOpNum = 16, kOpRat = 66, kOpDial = 38;

void OpRows::draw(CDrawContext* c) {
    if (ops_.empty()) return;
    const CRect r = getViewSize();
    const CCoord rowH = r.getHeight() / (CCoord)ops_.size();
    c->setDrawMode(kAntiAliasing);

    for (size_t i = 0; i < ops_.size(); i++) {
        const Op& o = ops_[i];
        const CCoord top = r.top + (CCoord)i * rowH;
        const float f = o.on ? 1.f : 0.34f;
        const CColor col = dim(o.colour, f);

        if ((int)i == sel_) {
            c->setFillColor(pal_.sel);
            c->drawRect(CRect(r.left, top, r.right, top + rowH), kDrawFilled);
        }

        c->setFont(mono_);
        c->setFontColor(dim(pal_.dim, f));
        char n[8]; snprintf(n, sizeof n, "%d", (int)i + 1);
        c->drawString(n, CRect(r.left + kGutter, top,
                               r.left + kGutter + kOpNum, top + rowH), kLeftText);

        c->setFont(big_);
        c->setFontColor(dim(pal_.ink, f));
        c->drawString(o.ratio.c_str(),
                      CRect(r.left + kGutter + kOpNum + 4, top,
                            r.left + kGutter + kOpNum + 4 + kOpRat, top + rowH), kLeftText);

        // Waveform, then the two dials, then the envelope. Widths are shares of
        // what is left after the labels, so the row works at whatever size it
        // is given rather than at one it was measured at once.
        const CCoord wx = r.left + kGutter + kOpNum + kOpRat + 12;
        const CCoord avail = r.right - wx;
        const CCoord envW = avail * 0.40;
        const CCoord ww = avail - envW - (kOpDial * 2 + 10) - 26;
        const CCoord dx = wx + ww + 16 + kOpDial * 0.5;
        const CCoord mid = top + rowH * 0.5;
        const CCoord dialR = std::min<CCoord>(17, rowH * 0.30);

        if (o.haveWave && ww > 20) {
            c->setFrameColor(col);
            c->setLineWidth(1.6);
            CPoint prev;
            for (int k = 0; k < 192; k++) {
                const CCoord x = wx + ww * k / 191.0;
                const CCoord y = mid - o.wave[k] * (rowH * 0.36);
                if (k) c->drawLine(prev, CPoint(x, y));
                prev = CPoint(x, y);
            }
            c->setLineWidth(1.0);
        }

        {   // level, as a ring
            c->setFrameColor(col);
            c->setLineWidth(2.0);
            c->drawEllipse(CRect(dx - dialR, mid - dialR, dx + dialR, mid + dialR),
                           kDrawStroked);
            c->setLineWidth(1.0);
            const CCoord ir = dialR * (0.22 + 0.42 * std::max(0.f, std::min(1.f, o.lev[0])));
            c->setFillColor(dim(pal_.ink, f));
            c->drawEllipse(CRect(dx - ir, mid - ir, dx + ir, mid + ir), kDrawFilled);
        }
        {   // feedback, as a diamond that fills in when there is any
            const CCoord cx = dx + kOpDial + 8;
            if (auto path = VSTGUI::owned(c->createGraphicsPath())) {
                path->beginSubpath(CPoint(cx, mid - dialR));
                path->addLine(CPoint(cx + dialR, mid));
                path->addLine(CPoint(cx, mid + dialR));
                path->addLine(CPoint(cx - dialR, mid));
                path->closeSubpath();
                c->setFrameColor(col);
                c->setLineWidth(2.0);
                c->drawGraphicsPath(path, CDrawContext::kPathStroked);
                c->setLineWidth(1.0);
            }
            if (o.fb) {
                const CCoord q = dialR * 0.5;
                if (auto path = VSTGUI::owned(c->createGraphicsPath())) {
                    path->beginSubpath(CPoint(cx, mid - q));
                    path->addLine(CPoint(cx + q, mid));
                    path->addLine(CPoint(cx, mid + q));
                    path->addLine(CPoint(cx - q, mid));
                    path->closeSubpath();
                    c->setFillColor(dim(pal_.ink, f));
                    c->drawGraphicsPath(path, CDrawContext::kPathFilled);
                }
            }
        }

        {   // the envelope, its x positions taken from the rates so a fast
            // attack really is a steep one
            const CCoord ex = r.right - envW, ew = envW;
            const CCoord eb = top + rowH - 9, eh = rowH - 20;
            if (ew > 30) {
                CCoord seg[4], total = 0, x[6];
                for (int k = 0; k < 4; k++) {
                    seg[k] = 0.06 + 0.30 * (1.0 - std::max(0.f, std::min(1.f, o.rt[k])));
                    total += seg[k];
                }
                x[0] = ex;
                for (int k = 0; k < 4; k++) x[k + 1] = x[k] + ew * 0.88 * seg[k] / total;
                x[5] = ex + ew;
                auto yOf = [&](float v) { return eb - eh * std::max(0.f, std::min(1.f, v)); };
                CPoint pts[6] = {
                    CPoint(x[0], yOf(0.f)),      CPoint(x[1], yOf(o.lev[0])),
                    CPoint(x[2], yOf(o.lev[1])), CPoint(x[3], yOf(o.lev[2])),
                    CPoint(x[4], yOf(o.lev[3])), CPoint(x[5], yOf(o.lev[3]))
                };
                if (auto path = VSTGUI::owned(c->createGraphicsPath())) {
                    path->beginSubpath(pts[0]);
                    for (int k = 1; k < 6; k++) path->addLine(pts[k]);
                    path->addLine(CPoint(x[5], eb));
                    path->addLine(CPoint(x[0], eb));
                    path->closeSubpath();
                    c->setFillColor(dim(col, f * 0.55f));
                    c->drawGraphicsPath(path, CDrawContext::kPathFilled);
                }
                c->setFrameColor(col);
                c->setLineWidth(1.6);
                for (int k = 1; k < 6; k++) c->drawLine(pts[k - 1], pts[k]);
                c->setLineWidth(1.0);
                c->setFillColor(dim(pal_.ink, f));
                for (int k = 1; k <= 4; k++)
                    c->drawRect(CRect(pts[k].x - 3, pts[k].y - 3,
                                      pts[k].x + 3, pts[k].y + 3), kDrawFilled);
            }
        }
    }
    drawRouting(c);
}

void OpRows::drawRouting(CDrawContext* c) {
    if (ops_.empty()) return;
    const CRect r = getViewSize();
    const CCoord rowH = r.getHeight() / (CCoord)ops_.size();
    auto midOf = [&](int i) { return r.top + (i + 0.5) * rowH; };

    // the output rail, spanning only the carriers that actually reach it
    int firstCar = -1, lastCar = -1;
    for (int i = 0; i < (int)ops_.size(); i++)
        if (ops_[i].carrier && ops_[i].on) { if (firstCar < 0) firstCar = i; lastCar = i; }
    if (firstCar >= 0 && lastCar > firstCar) {
        c->setFrameColor(pal_.lab);
        c->setLineWidth(1.6);
        c->drawLine(CPoint(r.left + kRailX, midOf(firstCar)),
                    CPoint(r.left + kRailX, midOf(lastCar)));
        c->setLineWidth(1.0);
    }

    for (size_t i = 0; i < ops_.size(); i++) {
        const Op& o = ops_[i];
        const float f = o.on ? 1.f : 0.30f;
        const CColor col = dim(o.colour, f);
        const CCoord my = midOf((int)i);
        const CCoord nx = r.left + kNodeX;

        c->setFrameColor(col);
        c->setLineWidth(1.6);
        if (o.carrier) {
            c->drawLine(CPoint(r.left + kRailX, my), CPoint(nx, my));
        } else if (o.target >= 0 && o.target < (int)ops_.size()) {
            const CCoord ch = r.left + kChanX + (CCoord)i * 5;
            const CCoord ty = midOf(o.target);
            c->drawLine(CPoint(nx, my), CPoint(ch, my));
            c->drawLine(CPoint(ch, my), CPoint(ch, ty));
            c->drawLine(CPoint(ch, ty), CPoint(nx, ty));
        }
        c->setLineWidth(1.0);

        CRect node(nx, my - 10, nx + kNodeW, my + 10);
        if (o.carrier) {
            c->setFillColor(col);
            c->drawRect(node, kDrawFilled);
        } else {
            c->setFillColor(pal_.bg);
            c->drawRect(node, kDrawFilled);
            c->setFrameColor(col);
            c->setLineWidth(1.6);
            c->drawRect(node, kDrawStroked);
            c->setLineWidth(1.0);
        }
        c->setFont(mono_);
        c->setFontColor(o.carrier ? pal_.bg : dim(pal_.ink, f));
        char n[8]; snprintf(n, sizeof n, "%d", (int)i + 1);
        c->drawString(n, node, kCenterText);

        if (o.fb) {   // a loop back into the same node
            const CCoord bx = nx + kNodeW;
            c->setFrameColor(col);
            c->setLineWidth(1.6);
            c->drawLine(CPoint(bx, my - 5), CPoint(bx + 7, my - 5));
            c->drawLine(CPoint(bx + 7, my - 5), CPoint(bx + 7, my + 5));
            c->drawLine(CPoint(bx + 7, my + 5), CPoint(bx, my + 5));
            c->setLineWidth(1.0);
        }
    }
}

void OpRows::onMouseDownEvent(MouseDownEvent& e) {
    if (!e.buttonState.isLeft() || ops_.empty()) return;
    const CRect r = getViewSize();
    const CCoord rowH = r.getHeight() / (CCoord)ops_.size();
    int i = (int)((e.mousePosition.y - r.top) / rowH);
    i = std::max(0, std::min((int)ops_.size() - 1, i));
    if (select_) select_(i);
    e.consumed = true;
}

} // namespace mui
