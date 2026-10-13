#include "MainWindow.h"
#include "DocumentTab.h"
#include "ThumbnailModel.h"

#include <QActionGroup>
#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QCryptographicHash>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QListView>
#include <QMimeData>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPdfBookmarkModel>
#include <QPdfDocument>
#include <QPdfPageNavigator>
#include <QPdfView>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QStyleHints>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QTreeView>

namespace {
// Zoom combo item data: positive values are percentages.
constexpr int kFitWidth = -1;
constexpr int kFitPage = -2;

// Settings key for a file's remembered reading position. Paths are hashed
// because QSettings treats '/' as a group separator.
QString fileStateKey(const QString &filePath)
{
    const QByteArray hash =
        QCryptographicHash::hash(filePath.toUtf8(), QCryptographicHash::Md5).toHex();
    return QStringLiteral("fileState/") + QString::fromLatin1(hash);
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("yapr"));
    setAcceptDrops(true);

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::currentTabChanged);

    // --- sidebar: outline + page thumbnails ---
    m_bookmarkModel = new QPdfBookmarkModel(this);
    m_outlineView = new QTreeView(this);
    m_outlineView->setModel(m_bookmarkModel);
    m_outlineView->setHeaderHidden(true);
    connect(m_outlineView, &QTreeView::activated, this, [this](const QModelIndex &index) {
        if (auto *tab = currentTab()) {
            const int page = index.data(int(QPdfBookmarkModel::Role::Page)).toInt();
            tab->goToPage(page);
        }
    });
    connect(m_outlineView, &QTreeView::clicked, m_outlineView, &QTreeView::activated);

    m_thumbnailModel = new ThumbnailModel(this);
    m_thumbnailView = new QListView(this);
    m_thumbnailView->setModel(m_thumbnailModel);
    m_thumbnailView->setViewMode(QListView::IconMode);
    m_thumbnailView->setMovement(QListView::Static);
    m_thumbnailView->setResizeMode(QListView::Adjust);
    m_thumbnailView->setSpacing(12);
    m_thumbnailView->setIconSize(QSize(ThumbnailModel::kThumbWidth, 4096));
    m_thumbnailView->setUniformItemSizes(false);
    m_thumbnailView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_thumbnailView, &QListView::clicked, this, [this](const QModelIndex &index) {
        if (auto *tab = currentTab())
            tab->goToPage(index.row());
    });

    auto *sidebarTabs = new QTabWidget(this);
    sidebarTabs->setDocumentMode(true);
    sidebarTabs->addTab(m_outlineView, tr("Outline"));
    sidebarTabs->addTab(m_thumbnailView, tr("Pages"));

    auto *sidebarDock = new QDockWidget(tr("Sidebar"), this);
    sidebarDock->setObjectName(QStringLiteral("sidebarDock"));
    sidebarDock->setWidget(sidebarTabs);
    sidebarDock->setMinimumWidth(ThumbnailModel::kThumbWidth + 60);
    addDockWidget(Qt::LeftDockWidgetArea, sidebarDock);
    sidebarDock->hide();

    // --- menu ---
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Open..."), QKeySequence::Open,
                        this, &MainWindow::openFileDialog);
    fileMenu->addAction(tr("&Close Tab"), QKeySequence(Qt::CTRL | Qt::Key_W), this, [this] {
        if (m_tabs->currentIndex() >= 0)
            closeTab(m_tabs->currentIndex());
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("Forget Reading &Positions"), this, [this] {
        QSettings settings;
        settings.remove(QStringLiteral("fileState"));
        statusBar()->showMessage(tr("Remembered reading positions cleared."), 3000);
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), QKeySequence(Qt::CTRL | Qt::Key_Q),
                        this, &QWidget::close);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    QAction *sidebarAction = sidebarDock->toggleViewAction();
    sidebarAction->setShortcut(QKeySequence(Qt::Key_F9));
    viewMenu->addAction(sidebarAction);
    viewMenu->addSeparator();
    viewMenu->addAction(tr("Zoom &In"), QKeySequence::ZoomIn,
                        this, [this] { zoomBy(1.2); });
    viewMenu->addAction(tr("Zoom &Out"), QKeySequence::ZoomOut,
                        this, [this] { zoomBy(1.0 / 1.2); });
    viewMenu->addSeparator();

    QSettings settings;

    QMenu *themeMenu = viewMenu->addMenu(tr("&Theme"));
    auto *themeGroup = new QActionGroup(this);
    const auto addThemeAction = [&](const QString &text, Qt::ColorScheme scheme) {
        QAction *action = themeMenu->addAction(text);
        action->setCheckable(true);
        themeGroup->addAction(action);
        connect(action, &QAction::triggered, this, [scheme] {
            QGuiApplication::styleHints()->setColorScheme(scheme);
            QSettings().setValue(QStringLiteral("ui/colorScheme"), int(scheme));
        });
        return action;
    };
    QAction *systemTheme = addThemeAction(tr("&System"), Qt::ColorScheme::Unknown);
    QAction *lightTheme = addThemeAction(tr("&Light"), Qt::ColorScheme::Light);
    QAction *darkTheme = addThemeAction(tr("&Dark"), Qt::ColorScheme::Dark);

    const auto savedScheme = Qt::ColorScheme(
        settings.value(QStringLiteral("ui/colorScheme"),
                       int(Qt::ColorScheme::Unknown)).toInt());
    switch (savedScheme) {
    case Qt::ColorScheme::Light: lightTheme->setChecked(true); break;
    case Qt::ColorScheme::Dark: darkTheme->setChecked(true); break;
    default: systemTheme->setChecked(true); break;
    }
    QGuiApplication::styleHints()->setColorScheme(savedScheme);

    QAction *invertAction = viewMenu->addAction(tr("Invert Page &Colors"));
    invertAction->setCheckable(true);
    invertAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    invertAction->setChecked(
        settings.value(QStringLiteral("view/invertColors"), false).toBool());
    connect(invertAction, &QAction::toggled, this, [this](bool inverted) {
        QSettings().setValue(QStringLiteral("view/invertColors"), inverted);
        for (int i = 0; i < m_tabs->count(); ++i) {
            if (auto *tab = qobject_cast<DocumentTab *>(m_tabs->widget(i)))
                tab->setInvertedColors(inverted);
        }
    });
    m_invertAction = invertAction;

    QMenu *editMenu = menuBar()->addMenu(tr("&Edit"));
    editMenu->addAction(tr("&Find..."), QKeySequence::Find, this, [this] {
        if (auto *tab = currentTab())
            tab->showSearchBar();
    });

    QMenu *helpMenu = menuBar()->addMenu(tr("&Help"));
    helpMenu->addAction(tr("&About"), this, [this] {
        QMessageBox::about(this, tr("About yapr"),
            tr("<b>yapr %1</b><br>yet another pdf reader.<br><br>"
               "Fast, predictable, no bloat.<br>"
               "Built with Qt %2. Licensed under the GPL-3.0.")
                .arg(QApplication::applicationVersion(), QLatin1String(qVersion())));
    });

    // --- toolbar ---
    auto *toolbar = addToolBar(tr("Main"));
    toolbar->setObjectName(QStringLiteral("mainToolbar"));
    toolbar->setMovable(false);

    toolbar->addAction(tr("Open"), QKeySequence(), this, &MainWindow::openFileDialog);
    toolbar->addSeparator();

    m_pageSpin = new QSpinBox(toolbar);
    m_pageSpin->setMinimum(1);
    m_pageSpin->setKeyboardTracking(false);
    connect(m_pageSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (auto *tab = currentTab(); tab && !m_restoringSession)
            tab->goToPage(value - 1);
    });
    toolbar->addWidget(m_pageSpin);

    m_pageCountLabel = new QLabel(toolbar);
    toolbar->addWidget(m_pageCountLabel);
    toolbar->addSeparator();

    m_zoomCombo = new QComboBox(toolbar);
    m_zoomCombo->addItem(tr("Fit Width"), kFitWidth);
    m_zoomCombo->addItem(tr("Fit Page"), kFitPage);
    for (int pct : {50, 75, 100, 125, 150, 200, 400})
        m_zoomCombo->addItem(QStringLiteral("%1%").arg(pct), pct);
    connect(m_zoomCombo, &QComboBox::activated, this, &MainWindow::applyZoomSelection);
    toolbar->addWidget(m_zoomCombo);

    statusBar();

    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());

    currentTabChanged(-1);
}

void MainWindow::openFile(const QString &filePath)
{
    const QString canonical = QFileInfo(filePath).absoluteFilePath();

    // Already open: switch to it. One file, one tab, always.
    for (int i = 0; i < m_tabs->count(); ++i) {
        auto *tab = qobject_cast<DocumentTab *>(m_tabs->widget(i));
        if (tab && tab->filePath() == canonical) {
            m_tabs->setCurrentIndex(i);
            return;
        }
    }

    auto *tab = new DocumentTab(canonical, this);
    if (!tab->isValid()) {
        delete tab;
        QMessageBox::warning(this, tr("yapr"),
                             tr("Could not open:\n%1").arg(canonical));
        return;
    }

    const int index = m_tabs->addTab(tab, QFileInfo(canonical).fileName());
    m_tabs->setTabToolTip(index, canonical);
    m_tabs->setCurrentIndex(index);
    tab->setInvertedColors(m_invertAction->isChecked());

    connect(tab, &DocumentTab::currentPageChanged, this, [this, tab](int) {
        if (tab == currentTab())
            syncPageControls();
    });

    // Restore the remembered reading position (page only — mode and zoom
    // stay at their deterministic defaults). Deferred so the view has a
    // real viewport size to lay pages out against first.
    QSettings settings;
    const int savedPage = settings.value(fileStateKey(canonical), 0).toInt();
    if (savedPage > 0 && savedPage < tab->document()->pageCount()) {
        QTimer::singleShot(0, tab, [tab, savedPage] {
            tab->goToPage(savedPage);
        });
    }

    saveSession();
}

void MainWindow::openFileDialog()
{
    QSettings settings;
    const QString startDir =
        settings.value(QStringLiteral("session/lastOpenDir")).toString();
    const QStringList files = QFileDialog::getOpenFileNames(
        this, tr("Open PDF"), startDir, tr("PDF files (*.pdf);;All files (*)"));
    if (files.isEmpty())
        return;
    settings.setValue(QStringLiteral("session/lastOpenDir"),
                      QFileInfo(files.first()).absolutePath());
    for (const QString &file : files)
        openFile(file);
}

void MainWindow::closeTab(int index)
{
    QWidget *widget = m_tabs->widget(index);
    if (auto *tab = qobject_cast<DocumentTab *>(widget))
        rememberReadingPosition(tab);
    m_tabs->removeTab(index);
    delete widget;
    // Closed means closed: persist immediately, not at exit.
    saveSession();
}

void MainWindow::rememberReadingPosition(DocumentTab *tab)
{
    QSettings settings;
    const QString key = fileStateKey(tab->filePath());
    if (tab->currentPage() > 0)
        settings.setValue(key, tab->currentPage());
    else
        settings.remove(key);
}

void MainWindow::currentTabChanged(int index)
{
    auto *tab = qobject_cast<DocumentTab *>(m_tabs->widget(index));

    const bool hasDoc = (tab != nullptr);
    m_pageSpin->setEnabled(hasDoc);
    m_zoomCombo->setEnabled(hasDoc);

    if (!hasDoc) {
        setWindowTitle(QStringLiteral("yapr"));
        m_pageCountLabel->setText(QStringLiteral(" / -"));
        m_bookmarkModel->setDocument(nullptr);
        m_thumbnailModel->setDocument(nullptr);
        statusBar()->clearMessage();
        return;
    }

    setWindowTitle(QStringLiteral("%1 — yapr")
                       .arg(QFileInfo(tab->filePath()).fileName()));
    m_bookmarkModel->setDocument(tab->document());
    m_outlineView->expandToDepth(0);
    m_thumbnailModel->setDocument(tab->document());
    statusBar()->showMessage(tab->filePath());
    syncPageControls();
}

void MainWindow::applyZoomSelection(int comboIndex)
{
    auto *tab = currentTab();
    if (!tab)
        return;
    const int data = m_zoomCombo->itemData(comboIndex).toInt();
    if (data == kFitWidth) {
        tab->view()->setZoomMode(QPdfView::ZoomMode::FitToWidth);
    } else if (data == kFitPage) {
        tab->view()->setZoomMode(QPdfView::ZoomMode::FitInView);
    } else {
        tab->view()->setZoomMode(QPdfView::ZoomMode::Custom);
        tab->view()->setZoomFactor(data / 100.0);
    }
}

void MainWindow::zoomBy(qreal factor)
{
    auto *tab = currentTab();
    if (!tab)
        return;
    const qreal current = tab->view()->zoomFactor();
    tab->view()->setZoomMode(QPdfView::ZoomMode::Custom);
    tab->view()->setZoomFactor(qBound(0.1, current * factor, 10.0));
}

void MainWindow::syncPageControls()
{
    auto *tab = currentTab();
    if (!tab)
        return;
    const int pageCount = tab->document()->pageCount();
    m_pageCountLabel->setText(QStringLiteral(" / %1").arg(pageCount));
    m_pageSpin->setMaximum(qMax(1, pageCount));
    const QSignalBlocker blocker(m_pageSpin);
    m_pageSpin->setValue(tab->currentPage() + 1);

    // Keep the thumbnail selection following the reading position.
    if (m_thumbnailView->isVisible()) {
        const QModelIndex idx = m_thumbnailModel->index(tab->currentPage());
        m_thumbnailView->setCurrentIndex(idx);
        m_thumbnailView->scrollTo(idx);
    }
}

void MainWindow::saveSession()
{
    if (m_restoringSession)
        return;
    QStringList files;
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *tab = qobject_cast<DocumentTab *>(m_tabs->widget(i)))
            files << tab->filePath();
    }
    QSettings settings;
    settings.setValue(QStringLiteral("session/openFiles"), files);
}

void MainWindow::restoreSession()
{
    QSettings settings;
    const QStringList files =
        settings.value(QStringLiteral("session/openFiles")).toStringList();
    m_restoringSession = true;
    for (const QString &file : files) {
        if (QFileInfo::exists(file))
            openFile(file);
    }
    m_restoringSession = false;
    saveSession();
}

DocumentTab *MainWindow::currentTab() const
{
    return qobject_cast<DocumentTab *>(m_tabs->currentWidget());
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    for (int i = 0; i < m_tabs->count(); ++i) {
        if (auto *tab = qobject_cast<DocumentTab *>(m_tabs->widget(i)))
            rememberReadingPosition(tab);
    }
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    for (const QUrl &url : urls) {
        if (url.isLocalFile())
            openFile(url.toLocalFile());
    }
    event->acceptProposedAction();
}
