#pragma once

#include <QHash>
#include <QPdfSelection>
#include <QPdfView>
#include <QPointF>
#include <QRect>
#include <QVector>

#include <optional>

class QTimer;

// QPdfView plus text selection and copy, which QPdfView lacks:
//   - drag to select text (across pages, with auto-scroll)
//   - double-click selects a word, Select All selects the document
//   - Ctrl+drag a rectangle to copy that region as a high-res image
//
// QPdfView keeps its page layout private, so pageGeometries() mirrors
// QPdfViewPrivate::calculateDocumentLayout() to map mouse positions to page
// coordinates. If a future Qt changes that layout, selection highlights
// will drift from the text — that's the coupling to watch.
class PdfView : public QPdfView
{
    Q_OBJECT

public:
    explicit PdfView(QWidget *parent = nullptr);

    bool hasSelection() const;
    QString selectedText() const;
    void selectAll();
    void clearSelection();
    void copy();

    // Scrolls so the previous/next page's top is at the top of the view.
    // "Previous" first returns to the top of the current page if you're
    // partway down it. Pure scrolling: no navigation-history entry.
    void stepPage(int delta);
    void scrollToPage(int page); // also no history entry

signals:
    // Short user-facing feedback (e.g. "Copied 120 characters").
    void statusMessage(const QString &message);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    struct PagePos {
        int page = -1;
        QPointF point; // PDF points, origin top-left of the page
        bool isValid() const { return page >= 0; }
    };
    enum class Mode { None, Range, Word, All };

    const QVector<QRect> &pageGeometries() const; // document coordinates
    QRectF viewportPageRect(int page) const;
    qreal pageScale(int page) const; // device-independent px per PDF point
    PagePos pageAt(const QPoint &viewportPos, bool clampToNearest) const;

    const QVector<QRectF> &textRects(int page) const;
    const QPdfSelection &fullPageText(int page) const;
    QPointF snapToText(int page, const QPointF &point) const;
    QPointF textStart(int page) const;
    QPointF textEnd(int page) const;
    std::optional<QPdfSelection> selectionForPage(int page) const;
    int selectionFirstPage() const;
    int selectionLastPage() const;

    void selectWordAt(const PagePos &pos);
    void copyRegionAsImage();
    void autoScrollTick();
    void invalidateSelectionCache();
    void resetDocumentCaches();

    // Layout cache, keyed on everything calculateDocumentLayout() reads.
    mutable QVector<QRect> m_geometries;
    mutable QSize m_layoutViewport;
    mutable const void *m_layoutDocument = nullptr;
    mutable int m_layoutPageCount = -1;
    mutable ZoomMode m_layoutZoomMode = ZoomMode::Custom;
    mutable qreal m_layoutZoomFactor = 0;

    mutable QHash<int, QPdfSelection> m_fullTextCache;
    mutable QHash<int, QVector<QRectF>> m_textRectCache;
    mutable QHash<int, std::optional<QPdfSelection>> m_selectionCache;

    Mode m_mode = Mode::None;
    PagePos m_anchor;
    PagePos m_head;
    std::optional<QPdfSelection> m_wordSelection;
    int m_wordPage = -1;

    bool m_selecting = false;
    bool m_dragged = false;
    QPoint m_pressPos;
    QPoint m_lastMousePos;
    QTimer *m_autoScrollTimer = nullptr;

    bool m_regionSelecting = false;
    int m_regionPage = -1;
    QPointF m_regionStart;
    QPointF m_regionEnd;
};
