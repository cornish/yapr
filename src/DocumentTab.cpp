#include "DocumentTab.h"
#include "PdfView.h"

#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfLink>
#include <QPdfPageNavigator>
#include <QPdfSearchModel>
#include <QMouseEvent>
#include <QPdfView>
#include <QScrollBar>
#include <QTimer>
#include <QVBoxLayout>

namespace {
// Files up to this size are read fully into memory, so the file on disk is
// never held open (or locked, on Windows) and can be rewritten or replaced
// underneath us — which is what makes auto-reload safe. Larger files are
// read lazily from disk to keep memory sane.
constexpr qint64 kMaxBufferedSize = qint64(512) * 1024 * 1024;

// Debounce for file-change notifications: writers like pdflatex touch the
// file several times; wait for it to settle before re-reading.
constexpr int kReloadDelayMs = 400;
constexpr int kMaxReloadAttempts = 10;
} // namespace

void InvertEffect::draw(QPainter *painter)
{
    QPoint offset;
    const QPixmap source =
        sourcePixmap(Qt::DeviceCoordinates, &offset, QGraphicsEffect::NoPad);
    if (source.isNull())
        return;
    QImage image = source.toImage();
    image.invertPixels();
    painter->save();
    painter->setWorldTransform(QTransform());
    painter->drawImage(offset, image);
    painter->restore();
}

DocumentTab::DocumentTab(const QString &filePath, QWidget *parent)
    : QWidget(parent)
    , m_filePath(filePath)
{
    m_document = new QPdfDocument(this);
    m_valid = loadInto(m_document, &m_buffer);

    m_view = new PdfView(this);
    m_view->setDocument(m_document);
    // Deterministic defaults: every document opens the same way, every time.
    m_view->setPageMode(QPdfView::PageMode::MultiPage);
    m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);

    m_searchModel = new QPdfSearchModel(this);
    m_searchModel->setDocument(m_document);
    m_view->setSearchModel(m_searchModel);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_view, 1);
    setFocusProxy(m_view);

    // --- wiring ---
    connect(m_view->pageNavigator(), &QPdfPageNavigator::currentPageChanged,
            this, &DocumentTab::currentPageChanged);
    connect(m_view->pageNavigator(), &QPdfPageNavigator::backAvailableChanged,
            this, &DocumentTab::historyChanged);
    connect(m_view->pageNavigator(), &QPdfPageNavigator::forwardAvailableChanged,
            this, &DocumentTab::historyChanged);

    // Mouse back/forward buttons.
    m_view->viewport()->installEventFilter(this);

    // Results arrive asynchronously as pages are scanned.
    connect(m_searchModel, &QPdfSearchModel::rowsInserted,
            this, &DocumentTab::searchStatusChanged);
    connect(m_searchModel, &QPdfSearchModel::modelReset,
            this, &DocumentTab::searchStatusChanged);

    m_reloadTimer = new QTimer(this);
    m_reloadTimer->setSingleShot(true);
    m_reloadTimer->setInterval(kReloadDelayMs);
    connect(m_reloadTimer, &QTimer::timeout, this, &DocumentTab::reloadFromWatcher);
}

int DocumentTab::currentPage() const
{
    return m_view->pageNavigator()->currentPage();
}

void DocumentTab::goToPage(int page)
{
    if (page < 0 || page >= m_document->pageCount())
        return;
    auto *nav = m_view->pageNavigator();
    nav->jump(page, QPointF(), nav->currentZoom());
}

void DocumentTab::stepPage(int delta)
{
    m_view->stepPage(delta);
}

void DocumentTab::restorePage(int page)
{
    if (page < 0 || page >= m_document->pageCount())
        return;
    // update() moves the view like jump() but rewrites the current history
    // entry instead of pushing one, so Back doesn't lead to page 1.
    auto *nav = m_view->pageNavigator();
    nav->update(page, QPointF(), nav->currentZoom());
}

void DocumentTab::goBack()
{
    m_view->pageNavigator()->back();
}

void DocumentTab::goForward()
{
    m_view->pageNavigator()->forward();
}

bool DocumentTab::canGoBack() const
{
    return m_view->pageNavigator()->backAvailable();
}

bool DocumentTab::canGoForward() const
{
    return m_view->pageNavigator()->forwardAvailable();
}

bool DocumentTab::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_view->viewport() && event->type() == QEvent::MouseButtonPress) {
        const auto *mouse = static_cast<QMouseEvent *>(event);
        if (mouse->button() == Qt::BackButton) {
            goBack();
            return true;
        }
        if (mouse->button() == Qt::ForwardButton) {
            goForward();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

bool DocumentTab::loadInto(QPdfDocument *document, QBuffer **buffer)
{
    *buffer = nullptr;
    if (QFileInfo(m_filePath).size() > kMaxBufferedSize)
        return document->load(m_filePath) == QPdfDocument::Error::None;

    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    auto *buf = new QBuffer(this);
    buf->setData(file.readAll());
    file.close();
    buf->open(QIODevice::ReadOnly);
    document->load(buf);
    if (document->status() != QPdfDocument::Status::Ready) {
        document->close();
        delete buf;
        return false;
    }
    *buffer = buf;
    return true;
}

bool DocumentTab::reload()
{
    // Load into a fresh document first, so a failed (half-written) reload
    // leaves the current one untouched on screen.
    auto *newDocument = new QPdfDocument(this);
    QBuffer *newBuffer = nullptr;
    if (!loadInto(newDocument, &newBuffer)) {
        delete newDocument;
        return false;
    }

    const int vScroll = m_view->verticalScrollBar()->value();
    const int hScroll = m_view->horizontalScrollBar()->value();
    const QString searchString = m_searchModel->searchString();

    QPdfDocument *oldDocument = m_document;
    QBuffer *oldBuffer = m_buffer;
    m_document = newDocument;
    m_buffer = newBuffer;

    m_view->setDocument(m_document);
    m_searchModel->setDocument(m_document);
    m_currentResult = -1;
    m_searchModel->setSearchString(QString());
    m_searchModel->setSearchString(searchString);
    emit searchStatusChanged();

    emit reloaded();

    delete oldDocument;
    delete oldBuffer;

    QTimer::singleShot(0, this, [this, vScroll, hScroll] {
        m_view->verticalScrollBar()->setValue(vScroll);
        m_view->horizontalScrollBar()->setValue(hScroll);
    });
    return true;
}

void DocumentTab::setAutoReload(bool enabled)
{
    if (enabled == (m_watcher != nullptr))
        return;
    if (enabled) {
        m_watcher = new QFileSystemWatcher(this);
        m_watcher->addPath(m_filePath);
        connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
            m_reloadAttempts = 0;
            m_reloadTimer->start();
        });
    } else {
        delete m_watcher;
        m_watcher = nullptr;
        m_reloadTimer->stop();
    }
}

void DocumentTab::reloadFromWatcher()
{
    if (!m_watcher)
        return;
    // Editors that save by delete+rename drop the watch; the file may also
    // be briefly missing or half-written. Retry a few times before giving up.
    const bool ok = QFileInfo::exists(m_filePath) && reload();
    if (QFileInfo::exists(m_filePath) && !m_watcher->files().contains(m_filePath))
        m_watcher->addPath(m_filePath);
    if (!ok && ++m_reloadAttempts < kMaxReloadAttempts)
        m_reloadTimer->start();
}

void DocumentTab::setInvertedColors(bool inverted)
{
    // Attach the effect only while inverting: an installed effect (even a
    // disabled one) costs a pixmap pass per paint and interferes with
    // repainting widgets that overlap the view.
    if (inverted == (m_view->viewport()->graphicsEffect() != nullptr))
        return;
    m_view->viewport()->setGraphicsEffect(inverted ? new InvertEffect : nullptr);
}

QString DocumentTab::searchText() const
{
    return m_searchModel->searchString();
}

void DocumentTab::setSearchText(const QString &text)
{
    if (text == m_searchModel->searchString())
        return;
    m_currentResult = -1;
    m_searchModel->setSearchString(text);
    emit searchStatusChanged();
}

void DocumentTab::findNext()
{
    const int count = m_searchModel->rowCount(QModelIndex());
    if (count == 0)
        return;
    jumpToResult((m_currentResult + 1) % count);
}

void DocumentTab::findPrevious()
{
    const int count = m_searchModel->rowCount(QModelIndex());
    if (count == 0)
        return;
    jumpToResult(m_currentResult <= 0 ? count - 1 : m_currentResult - 1);
}

void DocumentTab::jumpToResult(int index)
{
    m_currentResult = index;
    const QPdfLink link = m_searchModel->resultAtIndex(index);
    m_view->pageNavigator()->jump(link.page(), link.location(),
                                  m_view->pageNavigator()->currentZoom());
    m_view->setCurrentSearchResultIndex(index);
    emit searchStatusChanged();
}

QString DocumentTab::searchStatus() const
{
    if (m_searchModel->searchString().isEmpty())
        return QString();
    const int count = m_searchModel->rowCount(QModelIndex());
    if (count == 0)
        return tr("No matches");
    if (m_currentResult >= 0)
        return tr("%1 of %2").arg(m_currentResult + 1).arg(count);
    return tr("%n match(es)", nullptr, count);
}
