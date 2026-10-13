#include "MainWindow.h"
#include "DocumentTab.h"

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
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
#include <QTabWidget>
#include <QToolBar>
#include <QTreeView>

namespace {
// Zoom combo item data: positive values are percentages.
constexpr int kFitWidth = -1;
constexpr int kFitPage = -2;
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("yapr"));

    m_tabs = new QTabWidget(this);
    m_tabs->setTabsClosable(true);
    m_tabs->setMovable(true);
    m_tabs->setDocumentMode(true);
    setCentralWidget(m_tabs);

    connect(m_tabs, &QTabWidget::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabs, &QTabWidget::currentChanged, this, &MainWindow::currentTabChanged);

    // --- outline sidebar ---
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

    auto *outlineDock = new QDockWidget(tr("Outline"), this);
    outlineDock->setObjectName(QStringLiteral("outlineDock"));
    outlineDock->setWidget(m_outlineView);
    addDockWidget(Qt::LeftDockWidgetArea, outlineDock);
    outlineDock->hide();

    // --- menu ---
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(tr("&Open..."), QKeySequence::Open,
                        this, &MainWindow::openFileDialog);
    fileMenu->addAction(tr("&Close Tab"), QKeySequence(Qt::CTRL | Qt::Key_W), this, [this] {
        if (m_tabs->currentIndex() >= 0)
            closeTab(m_tabs->currentIndex());
    });
    fileMenu->addSeparator();
    fileMenu->addAction(tr("&Quit"), QKeySequence(Qt::CTRL | Qt::Key_Q),
                        this, &QWidget::close);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(outlineDock->toggleViewAction());
    viewMenu->addSeparator();
    viewMenu->addAction(tr("Zoom &In"), QKeySequence::ZoomIn,
                        this, [this] { zoomBy(1.2); });
    viewMenu->addAction(tr("Zoom &Out"), QKeySequence::ZoomOut,
                        this, [this] { zoomBy(1.0 / 1.2); });

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

    QSettings settings;
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

    connect(tab, &DocumentTab::currentPageChanged, this, [this, tab](int) {
        if (tab == currentTab())
            syncPageControls();
    });

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
    m_tabs->removeTab(index);
    delete widget;
    // Closed means closed: persist immediately, not at exit.
    saveSession();
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
        statusBar()->clearMessage();
        return;
    }

    setWindowTitle(QStringLiteral("%1 — yapr")
                       .arg(QFileInfo(tab->filePath()).fileName()));
    m_bookmarkModel->setDocument(tab->document());
    m_outlineView->expandToDepth(0);
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
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    QMainWindow::closeEvent(event);
}
