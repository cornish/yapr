#include "PdfView.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QGuiApplication>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QPdfDocument>
#include <QScreen>
#include <QScrollBar>
#include <QTimer>

#include <cmath>
#include <limits>

namespace {
// Target resolution for "copy region as image".
constexpr qreal kRegionDpi = 300.0;
// Cap on rendered page size for region copies (pixels), to bound memory.
constexpr qreal kMaxRenderPixels = 60.0e6;
constexpr int kDragThreshold = 4;

bool isWordChar(QChar c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_') || c == QLatin1Char('\'')
        || c == QChar(0x2019);
}
} // namespace

PdfView::PdfView(QWidget *parent)
    : QPdfView(parent)
{
    viewport()->setMouseTracking(true);

    m_autoScrollTimer = new QTimer(this);
    m_autoScrollTimer->setInterval(30);
    connect(m_autoScrollTimer, &QTimer::timeout, this, &PdfView::autoScrollTick);

    connect(this, &QPdfView::documentChanged, this, [this] {
        resetDocumentCaches();
        clearSelection();
    });
}

// ---------------------------------------------------------------------------
// Layout mirror

const QVector<QRect> &PdfView::pageGeometries() const
{
    QPdfDocument *doc = document();
    const int pageCount = (doc && doc->status() == QPdfDocument::Status::Ready)
        ? doc->pageCount() : 0;
    const QSize vp = viewport()->size();

    if (m_layoutDocument == doc && m_layoutPageCount == pageCount
        && m_layoutViewport == vp && m_layoutZoomMode == zoomMode()
        && m_layoutZoomFactor == zoomFactor()) {
        return m_geometries;
    }
    m_layoutDocument = doc;
    m_layoutPageCount = pageCount;
    m_layoutViewport = vp;
    m_layoutZoomMode = zoomMode();
    m_layoutZoomFactor = zoomFactor();
    m_geometries.clear();
    if (pageCount == 0)
        return m_geometries;

    // Mirrors QPdfViewPrivate::calculateDocumentLayout() (MultiPage mode).
    const qreal screenResolution =
        QGuiApplication::primaryScreen()->logicalDotsPerInch() / 72.0;
    const QMargins margins = documentMargins();
    const int spacing = pageSpacing();

    m_geometries.resize(pageCount);
    int totalWidth = 0;
    for (int page = 0; page < pageCount; ++page) {
        QSize pageSize;
        const QSizeF points = doc->pagePointSize(page);
        if (zoomMode() == ZoomMode::Custom) {
            pageSize = QSizeF(points * screenResolution * zoomFactor()).toSize();
        } else if (zoomMode() == ZoomMode::FitToWidth) {
            pageSize = QSizeF(points * screenResolution).toSize();
            const qreal factor =
                qreal(vp.width() - margins.left() - margins.right()) / qreal(pageSize.width());
            pageSize *= factor;
        } else { // FitInView
            const QSize available(vp.width() - margins.left() - margins.right(),
                                  vp.height() - spacing);
            pageSize = QSizeF(points * screenResolution).toSize();
            pageSize = pageSize.scaled(available, Qt::KeepAspectRatio);
        }
        totalWidth = qMax(totalWidth, pageSize.width());
        m_geometries[page] = QRect(QPoint(0, 0), pageSize);
    }
    totalWidth += margins.left() + margins.right();

    int pageY = margins.top();
    for (int page = 0; page < pageCount; ++page) {
        const QSize size = m_geometries[page].size();
        const int pageX = (qMax(totalWidth, vp.width()) - size.width()) / 2;
        m_geometries[page].moveTopLeft(QPoint(pageX, pageY));
        pageY += size.height() + spacing;
    }
    return m_geometries;
}

QRectF PdfView::viewportPageRect(int page) const
{
    const QVector<QRect> &geometries = pageGeometries();
    if (page < 0 || page >= geometries.size())
        return {};
    return QRectF(geometries[page].translated(-horizontalScrollBar()->value(),
                                              -verticalScrollBar()->value()));
}

qreal PdfView::pageScale(int page) const
{
    const QVector<QRect> &geometries = pageGeometries();
    if (page < 0 || page >= geometries.size())
        return 1.0;
    const qreal pointWidth = document()->pagePointSize(page).width();
    return pointWidth > 0 ? geometries[page].width() / pointWidth : 1.0;
}

PdfView::PagePos PdfView::pageAt(const QPoint &viewportPos, bool clampToNearest) const
{
    const QVector<QRect> &geometries = pageGeometries();
    if (geometries.isEmpty())
        return {};
    const QPoint docPos = viewportPos
        + QPoint(horizontalScrollBar()->value(), verticalScrollBar()->value());

    int page = -1;
    for (int i = 0; i < geometries.size(); ++i) {
        if (geometries[i].contains(docPos)) {
            page = i;
            break;
        }
    }
    if (page < 0) {
        if (!clampToNearest)
            return {};
        // Nearest page vertically (the gaps between pages, or above/below).
        int best = std::numeric_limits<int>::max();
        for (int i = 0; i < geometries.size(); ++i) {
            const QRect &r = geometries[i];
            const int dy = docPos.y() < r.top() ? r.top() - docPos.y()
                         : docPos.y() > r.bottom() ? docPos.y() - r.bottom() : 0;
            if (dy < best) {
                best = dy;
                page = i;
            }
        }
    }

    const QRect &r = geometries[page];
    const qreal scale = pageScale(page);
    QPointF point = QPointF(docPos - r.topLeft()) / scale;
    const QSizeF points = document()->pagePointSize(page);
    point.setX(qBound(0.0, point.x(), points.width()));
    point.setY(qBound(0.0, point.y(), points.height()));
    return {page, point};
}

// ---------------------------------------------------------------------------
// Text geometry

const QPdfSelection &PdfView::fullPageText(int page) const
{
    auto it = m_fullTextCache.find(page);
    if (it == m_fullTextCache.end())
        it = m_fullTextCache.insert(page, document()->getAllText(page));
    return it.value();
}

const QVector<QRectF> &PdfView::textRects(int page) const
{
    auto it = m_textRectCache.find(page);
    if (it == m_textRectCache.end()) {
        QVector<QRectF> rects;
        for (const QPolygonF &poly : fullPageText(page).bounds())
            rects << poly.boundingRect();
        it = m_textRectCache.insert(page, rects);
    }
    return it.value();
}

// QPdfDocument::getSelection() only finds characters within a small
// tolerance of the given points, so a drag starting in a margin or between
// lines would select nothing. Snap points onto the nearest text run first.
QPointF PdfView::snapToText(int page, const QPointF &point) const
{
    const QVector<QRectF> &rects = textRects(page);
    if (rects.isEmpty())
        return point;

    for (const QRectF &r : rects) {
        if (r.contains(point))
            return point;
    }

    // Prefer runs on the same line; pick the horizontally nearest.
    const QRectF *bestOnLine = nullptr;
    qreal bestDx = std::numeric_limits<qreal>::max();
    for (const QRectF &r : rects) {
        if (point.y() >= r.top() && point.y() <= r.bottom()) {
            const qreal dx = point.x() < r.left() ? r.left() - point.x() : point.x() - r.right();
            if (dx < bestDx) {
                bestDx = dx;
                bestOnLine = &r;
            }
        }
    }
    if (bestOnLine) {
        const QRectF &r = *bestOnLine;
        const qreal x = qBound(r.left() + 0.5, point.x(), r.right() - 0.5);
        return QPointF(x, r.center().y());
    }

    // Between lines: snap to the start of the line below or the end of the
    // line above, whichever is closer.
    const QRectF *nearest = nullptr;
    qreal bestDist = std::numeric_limits<qreal>::max();
    for (const QRectF &r : rects) {
        const qreal dy = point.y() < r.top() ? r.top() - point.y() : point.y() - r.bottom();
        const qreal dx = point.x() < r.left() ? r.left() - point.x()
                       : point.x() > r.right() ? point.x() - r.right() : 0;
        const qreal dist = dy * 4 + dx; // vertical distance dominates
        if (dist < bestDist) {
            bestDist = dist;
            nearest = &r;
        }
    }
    const QRectF &r = *nearest;
    if (point.y() < r.top())
        return QPointF(r.left() + 0.5, r.center().y());
    return QPointF(r.right() - 0.5, r.center().y());
}

QPointF PdfView::textStart(int page) const
{
    const QVector<QRectF> &rects = textRects(page);
    return rects.isEmpty() ? QPointF() : QPointF(rects.first().left() + 0.5,
                                                 rects.first().center().y());
}

QPointF PdfView::textEnd(int page) const
{
    const QVector<QRectF> &rects = textRects(page);
    return rects.isEmpty() ? QPointF() : QPointF(rects.last().right() - 0.5,
                                                 rects.last().center().y());
}

// ---------------------------------------------------------------------------
// Selection model

int PdfView::selectionFirstPage() const
{
    switch (m_mode) {
    case Mode::Range: return qMin(m_anchor.page, m_head.page);
    case Mode::Word: return m_wordPage;
    case Mode::All: return 0;
    case Mode::None: break;
    }
    return -1;
}

int PdfView::selectionLastPage() const
{
    switch (m_mode) {
    case Mode::Range: return qMax(m_anchor.page, m_head.page);
    case Mode::Word: return m_wordPage;
    case Mode::All: return document() ? document()->pageCount() - 1 : -1;
    case Mode::None: break;
    }
    return -1;
}

std::optional<QPdfSelection> PdfView::selectionForPage(int page) const
{
    if (m_mode == Mode::None || page < selectionFirstPage() || page > selectionLastPage())
        return std::nullopt;
    if (m_mode == Mode::Word)
        return m_wordSelection;
    if (m_mode == Mode::All)
        return fullPageText(page);

    auto it = m_selectionCache.find(page);
    if (it != m_selectionCache.end())
        return it.value();

    const bool forward = (m_anchor.page < m_head.page)
        || (m_anchor.page == m_head.page);
    const PagePos &first = forward ? m_anchor : m_head;
    const PagePos &last = forward ? m_head : m_anchor;

    std::optional<QPdfSelection> sel;
    if (textRects(page).isEmpty()) {
        sel = std::nullopt;
    } else if (first.page == last.page) {
        sel = document()->getSelection(page, snapToText(page, first.point),
                                       snapToText(page, last.point));
    } else if (page == first.page) {
        sel = document()->getSelection(page, snapToText(page, first.point), textEnd(page));
    } else if (page == last.page) {
        sel = document()->getSelection(page, textStart(page), snapToText(page, last.point));
    } else {
        sel = fullPageText(page);
    }
    m_selectionCache.insert(page, sel);
    return sel;
}

bool PdfView::hasSelection() const
{
    if (m_mode == Mode::None)
        return false;
    if (m_mode == Mode::All)
        return true;
    for (int page = selectionFirstPage(); page <= selectionLastPage(); ++page) {
        const auto sel = selectionForPage(page);
        if (sel && sel->isValid())
            return true;
    }
    return false;
}

QString PdfView::selectedText() const
{
    QStringList parts;
    for (int page = selectionFirstPage(); page >= 0 && page <= selectionLastPage(); ++page) {
        const auto sel = selectionForPage(page);
        if (sel && sel->isValid() && !sel->text().isEmpty())
            parts << sel->text();
    }
    return parts.join(QLatin1Char('\n'));
}

void PdfView::selectAll()
{
    if (!document() || document()->pageCount() == 0)
        return;
    m_mode = Mode::All;
    invalidateSelectionCache();
    viewport()->update();
}

void PdfView::clearSelection()
{
    if (m_mode == Mode::None)
        return;
    m_mode = Mode::None;
    m_wordPage = -1;
    m_wordSelection.reset();
    invalidateSelectionCache();
    viewport()->update();
}

void PdfView::copy()
{
    const QString text = selectedText();
    if (text.isEmpty())
        return;
    QGuiApplication::clipboard()->setText(text);
    emit statusMessage(tr("Copied %n character(s)", nullptr, int(text.size())));
}

void PdfView::stepPage(int delta)
{
    const QVector<QRect> &geometries = pageGeometries();
    if (geometries.isEmpty() || delta == 0)
        return;

    // Page at the top edge of the view (the gap below a page counts as
    // part of that page).
    const int top = verticalScrollBar()->value();
    const int margin = documentMargins().top();
    int page = 0;
    for (int i = 0; i < geometries.size(); ++i) {
        if (geometries[i].top() - margin <= top)
            page = i;
        else
            break;
    }

    int target;
    if (delta < 0) {
        // Partway into the page: go back to its top first.
        const bool atPageTop = top <= geometries[page].top() - margin + 2;
        target = atPageTop ? page - 1 : page;
    } else {
        target = page + 1;
    }
    scrollToPage(target);
}

void PdfView::scrollToPage(int page)
{
    const QVector<QRect> &geometries = pageGeometries();
    if (geometries.isEmpty())
        return;
    page = qBound(0, page, int(geometries.size()) - 1);
    verticalScrollBar()->setValue(geometries[page].top() - documentMargins().top());
}

void PdfView::invalidateSelectionCache()
{
    m_selectionCache.clear();
}

void PdfView::resetDocumentCaches()
{
    m_fullTextCache.clear();
    m_textRectCache.clear();
    m_selectionCache.clear();
    m_layoutDocument = nullptr;
    m_layoutPageCount = -1;
}

void PdfView::selectWordAt(const PagePos &pos)
{
    const QVector<QRectF> &rects = textRects(pos.page);
    const QRectF *hit = nullptr;
    for (const QRectF &r : rects) {
        if (r.adjusted(-1, -1, 1, 1).contains(pos.point)) {
            hit = &r;
            break;
        }
    }
    if (!hit)
        return;

    // Find the character index under the point: select from it to the end
    // of its run (or from the run start, if it is the last character).
    const QPointF runStart(hit->left() + 0.5, hit->center().y());
    const QPointF runEnd(hit->right() - 0.5, hit->center().y());
    const QPointF at(qBound(runStart.x(), pos.point.x(), runEnd.x()), hit->center().y());
    int index = -1;
    QPdfSelection probe = document()->getSelection(pos.page, at, runEnd);
    if (probe.isValid()) {
        index = probe.startIndex();
    } else {
        probe = document()->getSelection(pos.page, runStart, at);
        if (probe.isValid())
            index = probe.endIndex();
    }
    const QString text = fullPageText(pos.page).text();
    if (index < 0 || index >= text.size() || !isWordChar(text.at(index)))
        return;

    int start = index;
    int end = index;
    while (start > 0 && isWordChar(text.at(start - 1)))
        --start;
    while (end + 1 < text.size() && isWordChar(text.at(end + 1)))
        ++end;

    m_wordSelection = document()->getSelectionAtIndex(pos.page, start, end - start + 1);
    m_wordPage = pos.page;
    m_mode = m_wordSelection->isValid() ? Mode::Word : Mode::None;
    invalidateSelectionCache();
    viewport()->update();
}

void PdfView::copyRegionAsImage()
{
    const QRectF region = QRectF(m_regionStart, m_regionEnd).normalized();
    if (m_regionPage < 0 || region.width() < 2 || region.height() < 2)
        return;

    const QSizeF points = document()->pagePointSize(m_regionPage);
    qreal scale = kRegionDpi / 72.0;
    const qreal pixels = points.width() * points.height() * scale * scale;
    if (pixels > kMaxRenderPixels)
        scale *= std::sqrt(kMaxRenderPixels / pixels);

    const QImage page = document()->render(m_regionPage, (points * scale).toSize());
    if (page.isNull())
        return;
    const QRect crop = QRectF(region.topLeft() * scale, region.size() * scale)
                           .toAlignedRect()
                           .intersected(page.rect());
    const QImage image = page.copy(crop);
    QGuiApplication::clipboard()->setImage(image);
    emit statusMessage(tr("Copied image (%1 × %2 px)").arg(image.width()).arg(image.height()));
}

// ---------------------------------------------------------------------------
// Painting and input

void PdfView::paintEvent(QPaintEvent *event)
{
    QPdfView::paintEvent(event);

    const bool drawSelection = (m_mode != Mode::None);
    const bool drawRegion = m_regionSelecting && m_regionPage >= 0;
    if (!drawSelection && !drawRegion)
        return;

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QRectF visible = viewport()->rect();

    if (drawSelection) {
        QColor highlight = palette().color(QPalette::Highlight);
        highlight.setAlpha(80);
        painter.setPen(Qt::NoPen);
        painter.setBrush(highlight);
        const int first = qMax(0, selectionFirstPage());
        const int last = selectionLastPage();
        for (int page = first; page <= last; ++page) {
            const QRectF pageRect = viewportPageRect(page);
            if (pageRect.bottom() < visible.top())
                continue;
            if (pageRect.top() > visible.bottom())
                break;
            const auto sel = selectionForPage(page);
            if (!sel || !sel->isValid())
                continue;
            const qreal scale = pageScale(page);
            painter.setTransform(QTransform::fromTranslate(pageRect.x(), pageRect.y())
                                     .scale(scale, scale));
            for (const QPolygonF &poly : sel->bounds())
                painter.drawPolygon(poly);
        }
        painter.resetTransform();
    }

    if (drawRegion) {
        const QRectF pageRect = viewportPageRect(m_regionPage);
        const qreal scale = pageScale(m_regionPage);
        const QRectF r(pageRect.topLeft() + m_regionStart * scale,
                       pageRect.topLeft() + m_regionEnd * scale);
        QColor fill = palette().color(QPalette::Highlight);
        fill.setAlpha(40);
        painter.setBrush(fill);
        painter.setPen(QPen(palette().color(QPalette::Highlight), 1, Qt::DashLine));
        painter.drawRect(r.normalized());
    }
}

void PdfView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QPdfView::mousePressEvent(event);
        return;
    }

    if (event->modifiers() & Qt::ControlModifier) {
        const PagePos pos = pageAt(event->position().toPoint(), false);
        if (pos.isValid()) {
            m_regionSelecting = true;
            m_regionPage = pos.page;
            m_regionStart = m_regionEnd = pos.point;
            viewport()->setCursor(Qt::CrossCursor);
        }
        return;
    }

    QPdfView::mousePressEvent(event);
    clearSelection();
    const PagePos pos = pageAt(event->position().toPoint(), true);
    if (!pos.isValid())
        return;
    m_selecting = true;
    m_dragged = false;
    m_pressPos = m_lastMousePos = event->position().toPoint();
    m_anchor = m_head = pos;
}

void PdfView::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint pos = event->position().toPoint();

    if (m_regionSelecting) {
        const QRectF pageRect = viewportPageRect(m_regionPage);
        const qreal scale = pageScale(m_regionPage);
        const QSizeF points = document()->pagePointSize(m_regionPage);
        QPointF p = (QPointF(pos) - pageRect.topLeft()) / scale;
        p.setX(qBound(0.0, p.x(), points.width()));
        p.setY(qBound(0.0, p.y(), points.height()));
        m_regionEnd = p;
        viewport()->update();
        return;
    }

    if (m_selecting && (event->buttons() & Qt::LeftButton)) {
        m_lastMousePos = pos;
        if (!m_dragged && (pos - m_pressPos).manhattanLength() < kDragThreshold)
            return;
        m_dragged = true;
        m_mode = Mode::Range;
        m_head = pageAt(pos, true);
        invalidateSelectionCache();
        viewport()->update();
        const bool outside = pos.y() < 0 || pos.y() > viewport()->height();
        if (outside && !m_autoScrollTimer->isActive())
            m_autoScrollTimer->start();
        return;
    }

    // Hover: let QPdfView set the link cursor, then show an I-beam over text.
    QPdfView::mouseMoveEvent(event);
    if (viewport()->cursor().shape() == Qt::PointingHandCursor)
        return;
    const PagePos hover = pageAt(pos, false);
    bool overText = false;
    if (hover.isValid()) {
        for (const QRectF &r : textRects(hover.page)) {
            if (r.contains(hover.point)) {
                overText = true;
                break;
            }
        }
    }
    if (overText)
        viewport()->setCursor(Qt::IBeamCursor);
    else
        viewport()->unsetCursor();
}

void PdfView::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_regionSelecting && event->button() == Qt::LeftButton) {
        m_regionSelecting = false;
        viewport()->unsetCursor();
        copyRegionAsImage();
        m_regionPage = -1;
        viewport()->update();
        return;
    }

    if (m_selecting && event->button() == Qt::LeftButton) {
        m_selecting = false;
        m_autoScrollTimer->stop();
        if (m_dragged)
            return; // a drag is a selection, never a link click
    }
    QPdfView::mouseReleaseEvent(event);
}

void PdfView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QPdfView::mouseDoubleClickEvent(event);
        return;
    }
    m_selecting = false;
    const PagePos pos = pageAt(event->position().toPoint(), false);
    if (pos.isValid())
        selectWordAt(pos);
}

void PdfView::contextMenuEvent(QContextMenuEvent *event)
{
    QMenu menu(this);
    QAction *copyAction = menu.addAction(tr("&Copy"), this, &PdfView::copy);
    copyAction->setShortcut(QKeySequence::Copy);
    copyAction->setEnabled(hasSelection());
    menu.addAction(tr("Select &All"), QKeySequence::SelectAll, this, &PdfView::selectAll);
    menu.addSeparator();
    QAction *hint = menu.addAction(tr("Ctrl+drag to copy a region as an image"));
    hint->setEnabled(false);
    menu.exec(event->globalPos());
}

void PdfView::autoScrollTick()
{
    if (!m_selecting) {
        m_autoScrollTimer->stop();
        return;
    }
    const int h = viewport()->height();
    int dy = 0;
    if (m_lastMousePos.y() < 0)
        dy = m_lastMousePos.y();
    else if (m_lastMousePos.y() > h)
        dy = m_lastMousePos.y() - h;
    if (dy == 0) {
        m_autoScrollTimer->stop();
        return;
    }
    QScrollBar *bar = verticalScrollBar();
    bar->setValue(bar->value() + qBound(-60, dy, 60));
    m_head = pageAt(m_lastMousePos, true);
    invalidateSelectionCache();
    viewport()->update();
}
