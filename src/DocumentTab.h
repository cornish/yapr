#pragma once

#include <QGraphicsEffect>
#include <QWidget>

class PdfView;
class QBuffer;
class QFileSystemWatcher;
class QPdfDocument;
class QPdfSearchModel;
class QTimer;

// Inverts the rendered page colors ("dark reader" mode) by post-processing
// the viewport's painted output. The PDF engine itself has no invert knob.
class InvertEffect : public QGraphicsEffect
{
    Q_OBJECT

public:
    using QGraphicsEffect::QGraphicsEffect;

protected:
    void draw(QPainter *painter) override;
};

// One open PDF: the view, its document, and its search state. The search
// UI itself lives in the main toolbar and drives the current tab.
class DocumentTab : public QWidget
{
    Q_OBJECT

public:
    explicit DocumentTab(const QString &filePath, QWidget *parent = nullptr);

    bool isValid() const { return m_valid; }
    QString filePath() const { return m_filePath; }
    QPdfDocument *document() const { return m_document; }
    PdfView *view() const { return m_view; }

    int currentPage() const;
    void goToPage(int page); // 0-based; a navigation jump (adds history)
    void stepPage(int delta); // previous/next page; plain scrolling
    void restorePage(int page); // reopen at a remembered page; no history

    void setInvertedColors(bool inverted);

    // Search (driven by the toolbar Find box).
    QString searchText() const;
    void setSearchText(const QString &text);
    void findNext();
    void findPrevious();
    QString searchStatus() const; // e.g. "3 of 12", "No matches"

    // Link-style navigation history (links, outline, thumbnails, go-to-page).
    void goBack();
    void goForward();
    bool canGoBack() const;
    bool canGoForward() const;

    // Re-reads the file from disk, keeping the scroll position. On failure
    // (e.g. the file is mid-write) the current document stays on screen.
    bool reload();
    void setAutoReload(bool enabled);

signals:
    void currentPageChanged(int page); // 0-based
    void historyChanged();
    void searchStatusChanged();
    // Emitted after reload() swaps in a new document(), before the old one
    // is destroyed, so anything holding the old pointer can repoint.
    void reloaded();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    bool loadInto(QPdfDocument *document, QBuffer **buffer);
    void reloadFromWatcher();
    void jumpToResult(int index);

    QString m_filePath;
    bool m_valid = false;

    QPdfDocument *m_document = nullptr;
    QBuffer *m_buffer = nullptr; // in-memory copy of the file, if buffered
    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_reloadTimer = nullptr;
    int m_reloadAttempts = 0;

    PdfView *m_view = nullptr;
    QPdfSearchModel *m_searchModel = nullptr;
    int m_currentResult = -1;
};
