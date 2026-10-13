#include "DocumentTab.h"

#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPdfDocument>
#include <QPdfLink>
#include <QPdfPageNavigator>
#include <QPdfSearchModel>
#include <QPdfView>
#include <QShortcut>
#include <QToolButton>
#include <QVBoxLayout>

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
    m_valid = (m_document->load(filePath) == QPdfDocument::Error::None);

    m_view = new QPdfView(this);
    m_view->setDocument(m_document);
    // Deterministic defaults: every document opens the same way, every time.
    m_view->setPageMode(QPdfView::PageMode::MultiPage);
    m_view->setZoomMode(QPdfView::ZoomMode::FitToWidth);

    m_invertEffect = new InvertEffect(m_view->viewport());
    m_invertEffect->setEnabled(false);
    m_view->viewport()->setGraphicsEffect(m_invertEffect);

    m_searchModel = new QPdfSearchModel(this);
    m_searchModel->setDocument(m_document);
    m_view->setSearchModel(m_searchModel);

    // --- search bar (hidden until Ctrl+F) ---
    m_searchBar = new QWidget(this);
    auto *searchLayout = new QHBoxLayout(m_searchBar);
    searchLayout->setContentsMargins(4, 2, 4, 2);

    m_searchEdit = new QLineEdit(m_searchBar);
    m_searchEdit->setPlaceholderText(tr("Find in document..."));
    m_searchEdit->setClearButtonEnabled(true);

    m_searchCountLabel = new QLabel(m_searchBar);

    auto *prevButton = new QToolButton(m_searchBar);
    prevButton->setText(QStringLiteral("▲"));
    prevButton->setToolTip(tr("Previous match (Shift+F3)"));

    auto *nextButton = new QToolButton(m_searchBar);
    nextButton->setText(QStringLiteral("▼"));
    nextButton->setToolTip(tr("Next match (F3)"));

    auto *closeButton = new QToolButton(m_searchBar);
    closeButton->setText(QStringLiteral("✕"));
    closeButton->setToolTip(tr("Close search (Esc)"));

    searchLayout->addWidget(m_searchEdit, 1);
    searchLayout->addWidget(m_searchCountLabel);
    searchLayout->addWidget(prevButton);
    searchLayout->addWidget(nextButton);
    searchLayout->addWidget(closeButton);
    m_searchBar->hide();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(m_searchBar);
    layout->addWidget(m_view, 1);

    // --- wiring ---
    connect(m_view->pageNavigator(), &QPdfPageNavigator::currentPageChanged,
            this, &DocumentTab::currentPageChanged);

    connect(m_searchEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_currentResult = -1;
        m_searchModel->setSearchString(text);
        updateSearchLabel();
    });
    connect(m_searchEdit, &QLineEdit::returnPressed, this, &DocumentTab::findNext);
    connect(nextButton, &QToolButton::clicked, this, &DocumentTab::findNext);
    connect(prevButton, &QToolButton::clicked, this, &DocumentTab::findPrevious);
    connect(closeButton, &QToolButton::clicked, this, [this] {
        m_searchBar->hide();
        m_searchModel->setSearchString(QString());
        m_view->setFocus();
    });

    // Results arrive asynchronously as pages are scanned.
    connect(m_searchModel, &QPdfSearchModel::rowsInserted,
            this, &DocumentTab::updateSearchLabel);
    connect(m_searchModel, &QPdfSearchModel::modelReset,
            this, &DocumentTab::updateSearchLabel);

    auto *findNextShortcut = new QShortcut(QKeySequence(Qt::Key_F3), this);
    connect(findNextShortcut, &QShortcut::activated, this, &DocumentTab::findNext);
    auto *findPrevShortcut = new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_F3), this);
    connect(findPrevShortcut, &QShortcut::activated, this, &DocumentTab::findPrevious);
    auto *escShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_searchEdit);
    connect(escShortcut, &QShortcut::activated, closeButton, &QToolButton::click);
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

void DocumentTab::setInvertedColors(bool inverted)
{
    m_invertEffect->setEnabled(inverted);
}

void DocumentTab::showSearchBar()
{
    m_searchBar->show();
    m_searchEdit->setFocus();
    m_searchEdit->selectAll();
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
    updateSearchLabel();
}

void DocumentTab::updateSearchLabel()
{
    const int count = m_searchModel->rowCount(QModelIndex());
    if (m_searchModel->searchString().isEmpty())
        m_searchCountLabel->clear();
    else if (m_currentResult >= 0)
        m_searchCountLabel->setText(tr("%1 of %2").arg(m_currentResult + 1).arg(count));
    else
        m_searchCountLabel->setText(tr("%1 matches").arg(count));
}
