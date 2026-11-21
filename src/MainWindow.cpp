#include "MainWindow.h"
#include "DocumentTab.h"
#include "PdfView.h"
#include "ThumbnailModel.h"

#include <QActionGroup>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QGraphicsOpacityEffect>
#include <QHBoxLayout>
#include <QIconEngine>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPainterPath>
#include <QPdfBookmarkModel>
#include <QPdfDocument>
#include <QPdfDocumentRenderOptions>
#include <QPdfPageNavigator>
#include <QPrintDialog>
#include <QPrinter>
#include <QProcess>
#include <QProgressDialog>
#include <QSettings>
#include <QShortcut>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QStyleHints>
#include <QTabBar>
#include <QTabWidget>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeView>
#include <QVBoxLayout>

namespace {
enum ZoomPreset { kFitWidth, kFitPage, kActualSize };

constexpr int kMaxRecentFiles = 15;
constexpr int kMaxTabWidth = 240;
constexpr qreal kMaxPrintDpi = 300.0;
const QString kRecentFilesKey = QStringLiteral("recent/files");
const QString kAutoReloadKey = QStringLiteral("documents/autoReload");

// Settings key for a file's remembered reading position. Paths are hashed
// because QSettings treats '/' as a group separator.
QString fileStateKey(const QString &filePath)
{
    const QByteArray hash =
        QCryptographicHash::hash(filePath.toUtf8(), QCryptographicHash::Md5).toHex();
    return QStringLiteral("fileState/") + QString::fromLatin1(hash);
}

// Menu shortcuts only fire while their menu is reachable, and our menu
// lives behind the ☰ button — so register every action on the window too.
void addMenuActionsTo(QWidget *target, const QList<QAction *> &actions)
{
    for (QAction *action : actions) {
        if (action->menu())
            addMenuActionsTo(target, action->menu()->actions());
        else if (!action->isSeparator())
            target->addAction(action);
    }
}

// Simple line-art icons for the few glyphs the platform theme lacks (and
// fallbacks where a platform has no icon theme at all). Painted on demand
// in the current palette's text color, so they follow light/dark theme
// switches like the theme icons do.
class GlyphIconEngine : public QIconEngine
{
public:
    enum Glyph { FitWidth, FitPage, Menu, Contrast, ZoomIn, ZoomOut, Search, Plus, Print };
    explicit GlyphIconEngine(Glyph glyph) : m_glyph(glyph) {}

    void paint(QPainter *painter, const QRect &rect, QIcon::Mode mode, QIcon::State) override
    {
        const QPalette::ColorGroup group =
            (mode == QIcon::Disabled) ? QPalette::Disabled : QPalette::Normal;
        const QColor color = QApplication::palette().color(group, QPalette::WindowText);
        const qreal u = rect.width() / 16.0; // design on a 16x16 grid

        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        painter->translate(rect.topLeft());
        painter->scale(u, u);
        QPen pen(color, 1.2);
        pen.setCapStyle(Qt::RoundCap);
        pen.setJoinStyle(Qt::RoundJoin);
        painter->setPen(pen);
        painter->setBrush(Qt::NoBrush);

        switch (m_glyph) {
        case FitWidth:
            painter->drawLine(QPointF(1.5, 3), QPointF(1.5, 13));
            painter->drawLine(QPointF(14.5, 3), QPointF(14.5, 13));
            painter->drawLine(QPointF(4, 8), QPointF(12, 8));
            painter->drawPolyline(QPolygonF({{6, 6}, {4, 8}, {6, 10}}));
            painter->drawPolyline(QPolygonF({{10, 6}, {12, 8}, {10, 10}}));
            break;
        case FitPage:
            painter->drawRoundedRect(QRectF(3.5, 1.5, 9, 13), 1, 1);
            painter->drawLine(QPointF(8, 4.5), QPointF(8, 11.5));
            painter->drawPolyline(QPolygonF({{6.5, 6}, {8, 4.5}, {9.5, 6}}));
            painter->drawPolyline(QPolygonF({{6.5, 10}, {8, 11.5}, {9.5, 10}}));
            break;
        case Menu:
            painter->drawLine(QPointF(2.5, 4), QPointF(13.5, 4));
            painter->drawLine(QPointF(2.5, 8), QPointF(13.5, 8));
            painter->drawLine(QPointF(2.5, 12), QPointF(13.5, 12));
            break;
        case Contrast: {
            // Half-filled circle: the usual light/dark symbol, neutral to
            // whichever mode is current.
            const QRectF circle(2, 2, 12, 12);
            painter->drawEllipse(circle);
            QPainterPath half;
            half.moveTo(8, 2);
            half.arcTo(circle, 90, -180);
            half.closeSubpath();
            painter->setBrush(color);
            painter->drawPath(half);
            break;
        }
        case ZoomIn:
        case ZoomOut:
        case Search:
            painter->drawEllipse(QRectF(2, 2, 9, 9));
            painter->drawLine(QPointF(10, 10), QPointF(14, 14));
            if (m_glyph != Search)
                painter->drawLine(QPointF(4.5, 6.5), QPointF(8.5, 6.5));
            if (m_glyph == ZoomIn)
                painter->drawLine(QPointF(6.5, 4.5), QPointF(6.5, 8.5));
            break;
        case Plus:
            painter->drawLine(QPointF(8, 3), QPointF(8, 13));
            painter->drawLine(QPointF(3, 8), QPointF(13, 8));
            break;
        case Print:
            painter->drawRect(QRectF(4.5, 1.5, 7, 4));
            painter->drawRoundedRect(QRectF(1.5, 5.5, 13, 6), 1, 1);
            painter->drawRect(QRectF(4.5, 9.5, 7, 5));
            break;
        }
        painter->restore();
    }

    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap pm(size);
        pm.fill(Qt::transparent);
        QPainter painter(&pm);
        paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }

    QIconEngine *clone() const override { return new GlyphIconEngine(m_glyph); }

private:
    Glyph m_glyph;
};

QIcon glyphIcon(GlyphIconEngine::Glyph glyph)
{
    return QIcon(new GlyphIconEngine(glyph));
}

// Platform theme icon (Fluent on Windows, the desktop's icon theme on
// Linux), falling back to Qt's built-in style icons or our glyphs where
// the platform has none — so no toolbar button is ever blank.
QIcon themeIcon(QIcon::ThemeIcon icon)
{
    if (QIcon::hasThemeIcon(icon))
        return QIcon::fromTheme(icon);

    QStyle *style = QApplication::style();
    switch (icon) {
    case QIcon::ThemeIcon::FolderOpen: return style->standardIcon(QStyle::SP_DialogOpenButton);
    case QIcon::ThemeIcon::GoPrevious: return style->standardIcon(QStyle::SP_ArrowBack);
    case QIcon::ThemeIcon::GoNext: return style->standardIcon(QStyle::SP_ArrowForward);
    case QIcon::ThemeIcon::GoUp: return style->standardIcon(QStyle::SP_ArrowUp);
    case QIcon::ThemeIcon::GoDown: return style->standardIcon(QStyle::SP_ArrowDown);
    case QIcon::ThemeIcon::ViewRefresh: return style->standardIcon(QStyle::SP_BrowserReload);
    case QIcon::ThemeIcon::ViewFullscreen: return style->standardIcon(QStyle::SP_TitleBarMaxButton);
    case QIcon::ThemeIcon::ZoomIn: return glyphIcon(GlyphIconEngine::ZoomIn);
    case QIcon::ThemeIcon::ZoomOut: return glyphIcon(GlyphIconEngine::ZoomOut);
    case QIcon::ThemeIcon::EditFind: return glyphIcon(GlyphIconEngine::Search);
    case QIcon::ThemeIcon::ListAdd: return glyphIcon(GlyphIconEngine::Plus);
    case QIcon::ThemeIcon::DocumentPrint: return glyphIcon(GlyphIconEngine::Print);
    default: return QIcon();
    }
}

// Tabs show the file name, capped in width (long names elide) so many
// open documents stay readable.
class TabBar : public QTabBar
{
public:
    using QTabBar::QTabBar;

protected:
    QSize tabSizeHint(int index) const override
    {
        QSize size = QTabBar::tabSizeHint(index);
        size.setWidth(qMin(size.width(), kMaxTabWidth));
        return size;
    }
};
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    setWindowTitle(QStringLiteral("yapr"));
    setAcceptDrops(true);

    m_stack = new QStackedWidget(this);
    // Start page: the logo, faded, with a hint.
    m_emptyPage = new QWidget(m_stack);
    {
        auto *logo = new QLabel(m_emptyPage);
        QPixmap pixmap(QStringLiteral(":/icons/yapr-128.png"));
        pixmap.setDevicePixelRatio(1.0);
        logo->setPixmap(pixmap);
        logo->setAlignment(Qt::AlignCenter);
        auto *opacity = new QGraphicsOpacityEffect(logo);
        opacity->setOpacity(0.55);
        logo->setGraphicsEffect(opacity);
        auto *hint = new QLabel(tr("Open a PDF with Ctrl+O, or drop one here."), m_emptyPage);
        hint->setAlignment(Qt::AlignCenter);
        hint->setEnabled(false);
        auto *layout = new QVBoxLayout(m_emptyPage);
        layout->addStretch(1);
        layout->addWidget(logo);
        layout->addSpacing(12);
        layout->addWidget(hint);
        layout->addStretch(1);
    }
    m_stack->addWidget(m_emptyPage);
    setCentralWidget(m_stack);

    createSidebar();
    createActions();
    createMenu();
    createTabRow();
    createToolbar();

    // Transient messages (copy, reload, ...) float over the page instead of
    // occupying a permanent status bar.
    m_toast = new QLabel(this);
    m_toast->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_toast->setStyleSheet(QStringLiteral(
        "QLabel { background-color: palette(window); color: palette(window-text);"
        " border: 1px solid palette(mid); border-radius: 6px; padding: 6px 12px; }"));
    m_toast->hide();
    m_toastTimer = new QTimer(this);
    m_toastTimer->setSingleShot(true);
    connect(m_toastTimer, &QTimer::timeout, m_toast, &QWidget::hide);

    QSettings settings;
    restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray());
    restoreState(settings.value(QStringLiteral("window/state")).toByteArray());

    currentTabChanged(-1);
}

// ---------------------------------------------------------------------------
// UI construction

void MainWindow::createSidebar()
{
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

    m_sidebarAction = sidebarDock->toggleViewAction();
    m_sidebarAction->setText(tr("&Sidebar"));
    m_sidebarAction->setShortcut(QKeySequence(Qt::Key_F9));
}

void MainWindow::createActions()
{
    const auto make = [this](const QString &text, const QKeySequence &shortcut,
                             auto slot, const QIcon &icon = QIcon()) {
        auto *action = new QAction(icon, text, this);
        action->setShortcut(shortcut);
        connect(action, &QAction::triggered, this, slot);
        return action;
    };
    const auto withTab = [this](auto fn) {
        return [this, fn] {
            if (auto *tab = currentTab())
                fn(tab);
        };
    };

    m_openAction = make(tr("&Open..."), QKeySequence::Open,
                        [this] { openFileDialog(); }, themeIcon(QIcon::ThemeIcon::FolderOpen));
    m_printAction = make(tr("&Print..."), QKeySequence::Print,
                         [this] { printDocument(); }, themeIcon(QIcon::ThemeIcon::DocumentPrint));
    m_closeTabAction = make(tr("&Close Tab"), QKeySequence(Qt::CTRL | Qt::Key_W), [this] {
        if (m_tabBar->currentIndex() >= 0)
            closeTab(m_tabBar->currentIndex());
    });
    m_reopenAction = make(tr("Reopen Closed &Tab"),
                          QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_T),
                          [this] { reopenClosedTab(); });
    m_quitAction = make(tr("&Quit"), QKeySequence(Qt::CTRL | Qt::Key_Q),
                        [this] { close(); });

    m_copyAction = make(tr("&Copy"), QKeySequence::Copy,
                        withTab([](DocumentTab *t) { t->view()->copy(); }),
                        themeIcon(QIcon::ThemeIcon::EditCopy));
    m_selectAllAction = make(tr("Select &All"), QKeySequence::SelectAll,
                             withTab([](DocumentTab *t) { t->view()->selectAll(); }));
    m_findAction = make(tr("&Find..."), QKeySequence::Find, [this] { focusFind(); },
                        themeIcon(QIcon::ThemeIcon::EditFind));
    m_findNextAction = make(tr("Find &Next"), QKeySequence(Qt::Key_F3),
                            withTab([](DocumentTab *t) { t->findNext(); }),
                            themeIcon(QIcon::ThemeIcon::GoDown));
    m_findPrevAction = make(tr("Find Pre&vious"), QKeySequence(Qt::SHIFT | Qt::Key_F3),
                            withTab([](DocumentTab *t) { t->findPrevious(); }),
                            themeIcon(QIcon::ThemeIcon::GoUp));

    m_backAction = make(tr("&Back"), QKeySequence(Qt::ALT | Qt::Key_Left),
                        withTab([](DocumentTab *t) { t->goBack(); }));
    m_forwardAction = make(tr("&Forward"), QKeySequence(Qt::ALT | Qt::Key_Right),
                           withTab([](DocumentTab *t) { t->goForward(); }));
    m_goToPageAction = make(tr("&Go to Page..."), QKeySequence(Qt::CTRL | Qt::Key_G),
                            [this] { goToPageDialog(); });
    // Page stepping is plain scrolling, not navigation: it doesn't add
    // history entries (that's what Back/Forward are for).
    m_prevPageAction = make(tr("&Previous Page"), QKeySequence(),
                            withTab([](DocumentTab *t) { t->stepPage(-1); }),
                            themeIcon(QIcon::ThemeIcon::GoPrevious));
    m_nextPageAction = make(tr("&Next Page"), QKeySequence(),
                            withTab([](DocumentTab *t) { t->stepPage(+1); }),
                            themeIcon(QIcon::ThemeIcon::GoNext));

    // Zoom shortcuts follow Acrobat: Ctrl+0 fit page, Ctrl+1 actual size,
    // Ctrl+2 fit width.
    m_fitWidthAction = make(tr("Fit &Width"), QKeySequence(Qt::CTRL | Qt::Key_2),
                            [this] { setZoomMode(kFitWidth); },
                            glyphIcon(GlyphIconEngine::FitWidth));
    m_fitWidthAction->setCheckable(true);
    m_fitPageAction = make(tr("Fit &Page"), QKeySequence(Qt::CTRL | Qt::Key_0),
                           [this] { setZoomMode(kFitPage); },
                           glyphIcon(GlyphIconEngine::FitPage));
    m_fitPageAction->setCheckable(true);
    m_actualSizeAction = make(tr("&Actual Size"), QKeySequence(Qt::CTRL | Qt::Key_1),
                              [this] { setZoomMode(kActualSize); });
    m_zoomInAction = make(tr("Zoom &In"), QKeySequence::ZoomIn, [this] { zoomBy(1.2); },
                          themeIcon(QIcon::ThemeIcon::ZoomIn));
    m_zoomOutAction = make(tr("Zoom &Out"), QKeySequence::ZoomOut,
                           [this] { zoomBy(1.0 / 1.2); },
                           themeIcon(QIcon::ThemeIcon::ZoomOut));

    m_fullScreenAction = make(tr("&Full Screen"), QKeySequence(Qt::Key_F11),
                              [this](bool on) { setFullScreen(on); },
                              themeIcon(QIcon::ThemeIcon::ViewFullscreen));
    m_fullScreenAction->setCheckable(true);
    m_reloadAction = make(tr("&Reload"), QKeySequence(Qt::Key_F5),
                          [this] { reloadCurrentTab(); },
                          themeIcon(QIcon::ThemeIcon::ViewRefresh));

    QSettings settings;
    m_invertAction = new QAction(tr("&Invert Page Colors"), this);
    m_invertAction->setCheckable(true);
    m_invertAction->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    m_invertAction->setChecked(
        settings.value(QStringLiteral("view/invertColors"), false).toBool());
    connect(m_invertAction, &QAction::toggled, this, [this](bool inverted) {
        QSettings().setValue(QStringLiteral("view/invertColors"), inverted);
        for (int i = 0; i < tabCount(); ++i)
            tabAt(i)->setInvertedColors(inverted);
    });

    m_preferencesAction = make(tr("Pr&eferences..."), QKeySequence(Qt::CTRL | Qt::Key_Comma),
                               [this] { showPreferences(); });
}

void MainWindow::createMenu()
{
    m_mainMenu = new QMenu(this);
    m_mainMenu->addAction(m_openAction);
    m_recentMenu = m_mainMenu->addMenu(tr("Open &Recent"));
    m_recentMenu->setToolTipsVisible(true);
    connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
    m_mainMenu->addAction(m_printAction);
    m_mainMenu->addAction(m_closeTabAction);
    m_mainMenu->addAction(m_reopenAction);
    m_mainMenu->addSeparator();
    m_mainMenu->addAction(m_copyAction);
    m_mainMenu->addAction(m_selectAllAction);
    m_mainMenu->addAction(m_findAction);
    m_mainMenu->addSeparator();

    QMenu *viewMenu = m_mainMenu->addMenu(tr("&View"));
    viewMenu->addAction(m_sidebarAction);
    viewMenu->addAction(m_fullScreenAction);
    viewMenu->addAction(m_reloadAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_fitWidthAction);
    viewMenu->addAction(m_fitPageAction);
    viewMenu->addAction(m_actualSizeAction);
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
    viewMenu->addSeparator();

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
        QSettings().value(QStringLiteral("ui/colorScheme"),
                          int(Qt::ColorScheme::Unknown)).toInt());
    switch (savedScheme) {
    case Qt::ColorScheme::Light: lightTheme->setChecked(true); break;
    case Qt::ColorScheme::Dark: darkTheme->setChecked(true); break;
    default: systemTheme->setChecked(true); break;
    }
    QGuiApplication::styleHints()->setColorScheme(savedScheme);

    // One-click flip between light and dark (from whatever is showing now,
    // including a System theme). Lives on the toolbar and in this menu.
    themeMenu->addSeparator();
    m_themeToggleAction = themeMenu->addAction(
        glyphIcon(GlyphIconEngine::Contrast), tr("&Toggle Light/Dark"));
    m_themeToggleAction->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
    connect(m_themeToggleAction, &QAction::triggered, this, [lightTheme, darkTheme] {
        const bool dark =
            QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
        (dark ? lightTheme : darkTheme)->trigger();
    });
    viewMenu->addAction(m_invertAction);

    QMenu *goMenu = m_mainMenu->addMenu(tr("&Go"));
    goMenu->addAction(m_prevPageAction);
    goMenu->addAction(m_nextPageAction);
    goMenu->addSeparator();
    goMenu->addAction(m_backAction);
    goMenu->addAction(m_forwardAction);
    goMenu->addAction(m_goToPageAction);
    goMenu->addSeparator();
    goMenu->addAction(m_findNextAction);
    goMenu->addAction(m_findPrevAction);

    m_mainMenu->addSeparator();
    m_mainMenu->addAction(m_preferencesAction);
    m_mainMenu->addAction(tr("Forget Reading P&ositions"), this, [this] {
        QSettings().remove(QStringLiteral("fileState"));
        showToast(tr("Remembered reading positions cleared."));
    });
    m_mainMenu->addAction(tr("A&bout yapr"), this, [this] {
        QMessageBox::about(this, tr("About yapr"),
            tr("<b>yapr %1</b><br>yet another pdf reader.<br><br>"
               "Fast, predictable, no bloat.<br>"
               "Built with Qt %2. Licensed under the GPL-3.0.")
                .arg(QApplication::applicationVersion(), QLatin1String(qVersion())));
    });
    m_mainMenu->addSeparator();
    m_mainMenu->addAction(m_quitAction);

    addMenuActionsTo(this, m_mainMenu->actions());
}

void MainWindow::createTabRow()
{
    m_tabRow = new QWidget(this);
    auto *layout = new QHBoxLayout(m_tabRow);
    layout->setContentsMargins(4, 4, 4, 0);
    layout->setSpacing(2);

    // ☰ opens the menu on click (no drop-down arrow, unlike a menu button).
    auto *menuButton = new QToolButton(m_tabRow);
    menuButton->setIcon(glyphIcon(GlyphIconEngine::Menu));
    menuButton->setAutoRaise(true);
    menuButton->setToolTip(tr("Menu"));
    connect(menuButton, &QToolButton::clicked, this, [this, menuButton] {
        m_mainMenu->exec(menuButton->mapToGlobal(QPoint(0, menuButton->height())));
    });

    m_tabBar = new TabBar(m_tabRow);
    m_tabBar->setTabsClosable(true);
    m_tabBar->setMovable(true);
    m_tabBar->setDocumentMode(true);
    m_tabBar->setExpanding(false);
    m_tabBar->setElideMode(Qt::ElideRight);
    m_tabBar->setUsesScrollButtons(true);
    m_tabBar->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_tabBar, &QTabBar::currentChanged, this, &MainWindow::currentTabChanged);
    connect(m_tabBar, &QTabBar::tabCloseRequested, this, &MainWindow::closeTab);
    connect(m_tabBar, &QTabBar::tabMoved, this, [this] { saveSession(); });
    connect(m_tabBar, &QWidget::customContextMenuRequested,
            this, &MainWindow::showTabContextMenu);

    auto *newTabButton = new QToolButton(m_tabRow);
    newTabButton->setIcon(themeIcon(QIcon::ThemeIcon::ListAdd));
    newTabButton->setAutoRaise(true);
    newTabButton->setToolTip(tr("Open a file (Ctrl+O)"));
    connect(newTabButton, &QToolButton::clicked, this, &MainWindow::openFileDialog);

    layout->addWidget(menuButton);
    layout->addWidget(m_tabBar);
    layout->addWidget(newTabButton);
    layout->addStretch(1);

    setMenuWidget(m_tabRow);
}

void MainWindow::createToolbar()
{
    m_toolbar = addToolBar(tr("Main"));
    m_toolbar->setObjectName(QStringLiteral("mainToolbar"));
    m_toolbar->setMovable(false);
    m_toolbar->setFloatable(false);
    m_toolbar->toggleViewAction()->setVisible(false);
    m_toolbar->setIconSize(QSize(18, 18));

    // Tooltips show the shortcut, since there's no menu bar to show it.
    const auto add = [this](QAction *action) {
        m_toolbar->addAction(action);
        if (!action->shortcut().isEmpty()) {
            action->setToolTip(QStringLiteral("%1 (%2)")
                                   .arg(action->text().remove(QLatin1Char('&'))
                                            .remove(QStringLiteral("...")),
                                        action->shortcut().toString(QKeySequence::NativeText)));
        }
    };

    add(m_openAction);
    add(m_printAction);
    m_toolbar->addSeparator();

    m_toolbar->addWidget(new QLabel(tr("Page: "), m_toolbar));
    m_pageSpin = new QSpinBox(m_toolbar);
    m_pageSpin->setMinimum(1);
    m_pageSpin->setKeyboardTracking(false);
    m_pageSpin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_pageSpin->setAlignment(Qt::AlignRight);
    m_pageSpin->setFixedWidth(52);
    connect(m_pageSpin, &QSpinBox::valueChanged, this, [this](int value) {
        if (auto *tab = currentTab(); tab && !m_restoringSession)
            tab->goToPage(value - 1);
    });
    // After typing a page number + Enter, hand the keyboard back to the page.
    // (editingFinished also fires on focus loss, hence the hasFocus check.)
    connect(m_pageSpin, &QSpinBox::editingFinished, this, [this] {
        if (auto *tab = currentTab(); tab && m_pageSpin->hasFocus())
            tab->view()->setFocus();
    });
    m_toolbar->addWidget(m_pageSpin);
    m_pageCountLabel = new QLabel(m_toolbar);
    m_toolbar->addWidget(m_pageCountLabel);
    add(m_prevPageAction);
    add(m_nextPageAction);
    m_toolbar->addSeparator();

    add(m_fitWidthAction);
    add(m_fitPageAction);
    add(m_zoomOutAction);
    add(m_zoomInAction);
    m_toolbar->addSeparator();

    m_toolbar->addWidget(new QLabel(tr("Find: "), m_toolbar));
    m_findEdit = new QLineEdit(m_toolbar);
    m_findEdit->setClearButtonEnabled(true);
    m_findEdit->setFixedWidth(180);
    connect(m_findEdit, &QLineEdit::textChanged, this, [this](const QString &text) {
        if (auto *tab = currentTab())
            tab->setSearchText(text);
    });
    connect(m_findEdit, &QLineEdit::returnPressed, m_findNextAction, &QAction::trigger);
    auto *findPrevShortcut =
        new QShortcut(QKeySequence(Qt::SHIFT | Qt::Key_Return), m_findEdit);
    findPrevShortcut->setContext(Qt::WidgetShortcut);
    connect(findPrevShortcut, &QShortcut::activated, m_findPrevAction, &QAction::trigger);
    auto *findEscShortcut = new QShortcut(QKeySequence(Qt::Key_Escape), m_findEdit);
    findEscShortcut->setContext(Qt::WidgetShortcut);
    connect(findEscShortcut, &QShortcut::activated, this, [this] {
        m_findEdit->clear();
        if (auto *tab = currentTab())
            tab->view()->setFocus();
    });
    m_toolbar->addWidget(m_findEdit);
    add(m_findPrevAction);
    add(m_findNextAction);
    m_findStatusLabel = new QLabel(m_toolbar);
    m_findStatusLabel->setContentsMargins(6, 0, 0, 0);
    m_toolbar->addWidget(m_findStatusLabel);

    // Theme toggle sits at the far right.
    auto *spacer = new QWidget(m_toolbar);
    spacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    m_toolbar->addWidget(spacer);
    add(m_themeToggleAction);
}

// ---------------------------------------------------------------------------
// Tabs

int MainWindow::tabCount() const
{
    return m_tabBar->count();
}

DocumentTab *MainWindow::tabAt(int index) const
{
    if (index < 0 || index >= m_tabBar->count())
        return nullptr;
    return qobject_cast<DocumentTab *>(m_tabBar->tabData(index).value<QObject *>());
}

int MainWindow::indexOfTab(DocumentTab *tab) const
{
    for (int i = 0; i < tabCount(); ++i) {
        if (tabAt(i) == tab)
            return i;
    }
    return -1;
}

DocumentTab *MainWindow::currentTab() const
{
    return tabAt(m_tabBar->currentIndex());
}

void MainWindow::openFile(const QString &filePath)
{
    const QString canonical = QFileInfo(filePath).absoluteFilePath();

    // Already open: switch to it. One file, one tab, always.
    for (int i = 0; i < tabCount(); ++i) {
        if (tabAt(i)->filePath() == canonical) {
            m_tabBar->setCurrentIndex(i);
            return;
        }
    }

    auto *tab = new DocumentTab(canonical, this);
    if (!tab->isValid()) {
        delete tab;
        QMessageBox::warning(this, tr("yapr"),
                             tr("Could not open:\n%1").arg(QDir::toNativeSeparators(canonical)));
        return;
    }

    tab->setInvertedColors(m_invertAction->isChecked());
    tab->setAutoReload(autoReloadEnabled());

    connect(tab, &DocumentTab::currentPageChanged, this, [this, tab](int) {
        if (tab == currentTab())
            syncPageControls();
    });
    connect(tab, &DocumentTab::historyChanged, this, [this, tab] {
        if (tab == currentTab())
            syncHistoryActions();
    });
    connect(tab, &DocumentTab::searchStatusChanged, this, [this, tab] {
        if (tab == currentTab())
            syncSearchControls();
    });
    connect(tab->view(), &QPdfView::zoomModeChanged, this, [this, tab] {
        if (tab == currentTab())
            syncZoomActions();
    });
    connect(tab->view(), &PdfView::statusMessage, this, [this](const QString &message) {
        showToast(message);
    });
    connect(tab, &DocumentTab::reloaded, this, [this, tab] {
        if (tab != currentTab())
            return;
        m_bookmarkModel->setDocument(tab->document());
        m_outlineView->expandToDepth(0);
        m_thumbnailModel->setDocument(tab->document());
        syncPageControls();
        showToast(tr("Reloaded"));
    });

    m_stack->addWidget(tab);
    int index;
    {
        // The first tab becomes current inside addTab(), before its data is
        // set; hold signals and announce the change ourselves.
        const QSignalBlocker blocker(m_tabBar);
        index = m_tabBar->addTab(QFileInfo(canonical).fileName());
        m_tabBar->setTabData(index, QVariant::fromValue(static_cast<QObject *>(tab)));
        m_tabBar->setTabToolTip(index, QDir::toNativeSeparators(canonical));
        m_tabBar->setCurrentIndex(index);
    }
    currentTabChanged(index);

    // Restore the remembered reading position (page only — mode and zoom
    // stay at their deterministic defaults). Deferred so the view has a
    // real viewport size to lay pages out against first.
    QSettings settings;
    const int savedPage = settings.value(fileStateKey(canonical), 0).toInt();
    if (savedPage > 0 && savedPage < tab->document()->pageCount()) {
        QTimer::singleShot(0, tab, [tab, savedPage] {
            tab->restorePage(savedPage);
        });
    }

    m_closedFiles.removeAll(canonical);
    if (!m_restoringSession)
        addRecentFile(canonical);
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
    DocumentTab *tab = tabAt(index);
    if (!tab)
        return;
    rememberReadingPosition(tab);
    m_closedFiles.removeAll(tab->filePath());
    m_closedFiles.append(tab->filePath());

    m_tabBar->removeTab(index); // switches the stack to the next tab
    m_stack->removeWidget(tab);
    delete tab;
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
    DocumentTab *tab = tabAt(index);
    const bool hasDoc = (tab != nullptr);

    m_stack->setCurrentWidget(hasDoc ? static_cast<QWidget *>(tab) : m_emptyPage);
    for (QAction *action : {m_printAction, m_closeTabAction, m_copyAction, m_selectAllAction,
                            m_findAction, m_findNextAction, m_findPrevAction,
                            m_goToPageAction, m_prevPageAction, m_nextPageAction,
                            m_fitWidthAction, m_fitPageAction,
                            m_actualSizeAction, m_zoomInAction, m_zoomOutAction,
                            m_reloadAction}) {
        action->setEnabled(hasDoc);
    }
    m_pageSpin->setEnabled(hasDoc);
    m_findEdit->setEnabled(hasDoc);
    syncHistoryActions();
    syncZoomActions();
    syncSearchControls();

    if (!hasDoc) {
        setWindowTitle(QStringLiteral("yapr"));
        m_pageCountLabel->setText(QStringLiteral(" / -"));
        m_bookmarkModel->setDocument(nullptr);
        m_thumbnailModel->setDocument(nullptr);
        return;
    }

    setWindowTitle(QStringLiteral("%1 — yapr").arg(QFileInfo(tab->filePath()).fileName()));
    m_bookmarkModel->setDocument(tab->document());
    m_outlineView->expandToDepth(0);
    m_thumbnailModel->setDocument(tab->document());
    syncPageControls();
    tab->view()->setFocus();
}

// ---------------------------------------------------------------------------
// Zoom, page, history, search

void MainWindow::setZoomMode(int mode)
{
    auto *tab = currentTab();
    if (!tab)
        return;
    switch (mode) {
    case kFitWidth:
        tab->view()->setZoomMode(QPdfView::ZoomMode::FitToWidth);
        break;
    case kFitPage:
        tab->view()->setZoomMode(QPdfView::ZoomMode::FitInView);
        break;
    default:
        tab->view()->setZoomMode(QPdfView::ZoomMode::Custom);
        tab->view()->setZoomFactor(1.0);
        break;
    }
    syncZoomActions();
}

void MainWindow::zoomBy(qreal factor)
{
    auto *tab = currentTab();
    if (!tab)
        return;
    const qreal current = tab->view()->zoomFactor();
    tab->view()->setZoomMode(QPdfView::ZoomMode::Custom);
    tab->view()->setZoomFactor(qBound(0.1, current * factor, 10.0));
    syncZoomActions();
}

void MainWindow::syncZoomActions()
{
    auto *tab = currentTab();
    const auto mode = tab ? tab->view()->zoomMode() : QPdfView::ZoomMode::Custom;
    m_fitWidthAction->setChecked(tab && mode == QPdfView::ZoomMode::FitToWidth);
    m_fitPageAction->setChecked(tab && mode == QPdfView::ZoomMode::FitInView);
}

void MainWindow::syncPageControls()
{
    auto *tab = currentTab();
    if (!tab)
        return;
    const int pageCount = tab->document()->pageCount();
    m_pageCountLabel->setText(QStringLiteral(" / %1 ").arg(pageCount));
    m_pageSpin->setMaximum(qMax(1, pageCount));
    const QSignalBlocker blocker(m_pageSpin);
    m_pageSpin->setValue(tab->currentPage() + 1);
    m_prevPageAction->setEnabled(tab->currentPage() > 0);
    m_nextPageAction->setEnabled(tab->currentPage() < pageCount - 1);

    // Keep the thumbnail selection following the reading position.
    if (m_thumbnailView->isVisible()) {
        const QModelIndex idx = m_thumbnailModel->index(tab->currentPage());
        m_thumbnailView->setCurrentIndex(idx);
        m_thumbnailView->scrollTo(idx);
    }
}

void MainWindow::syncHistoryActions()
{
    auto *tab = currentTab();
    m_backAction->setEnabled(tab && tab->canGoBack());
    m_forwardAction->setEnabled(tab && tab->canGoForward());
}

void MainWindow::syncSearchControls()
{
    auto *tab = currentTab();
    if (m_findEdit->text() != (tab ? tab->searchText() : QString())) {
        const QSignalBlocker blocker(m_findEdit);
        m_findEdit->setText(tab ? tab->searchText() : QString());
    }
    m_findStatusLabel->setText(tab ? tab->searchStatus() : QString());
}

void MainWindow::focusFind()
{
    if (!currentTab())
        return;
    if (!m_toolbar->isVisible())
        setFullScreen(false);
    m_findEdit->setFocus();
    m_findEdit->selectAll();
}

void MainWindow::goToPageDialog()
{
    auto *tab = currentTab();
    if (!tab)
        return;
    if (m_pageSpin->isVisible()) {
        m_pageSpin->setFocus();
        m_pageSpin->selectAll();
        return;
    }
    // Toolbar hidden (full screen): ask in a small dialog instead.
    bool ok = false;
    const int page = QInputDialog::getInt(this, tr("Go to Page"), tr("Page:"),
                                          tab->currentPage() + 1, 1,
                                          tab->document()->pageCount(), 1, &ok);
    if (ok)
        tab->goToPage(page - 1);
}

// ---------------------------------------------------------------------------
// Session, recent files, reopen

void MainWindow::saveSession()
{
    if (m_restoringSession)
        return;
    QStringList files;
    for (int i = 0; i < tabCount(); ++i)
        files << tabAt(i)->filePath();
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

void MainWindow::addRecentFile(const QString &filePath)
{
    QSettings settings;
    QStringList files = settings.value(kRecentFilesKey).toStringList();
    files.removeAll(filePath);
    files.prepend(filePath);
    while (files.size() > kMaxRecentFiles)
        files.removeLast();
    settings.setValue(kRecentFilesKey, files);
}

void MainWindow::rebuildRecentMenu()
{
    m_recentMenu->clear();
    const QStringList files = QSettings().value(kRecentFilesKey).toStringList();
    int shown = 0;
    for (const QString &file : files) {
        if (!QFileInfo::exists(file))
            continue;
        ++shown;
        // Number the first nine for keyboard access.
        const QString name = QFileInfo(file).fileName();
        const QString label = shown <= 9
            ? QStringLiteral("&%1  %2").arg(shown).arg(name)
            : name;
        QAction *action = m_recentMenu->addAction(label, this, [this, file] {
            openFile(file);
        });
        action->setToolTip(QDir::toNativeSeparators(file));
    }
    if (shown == 0)
        m_recentMenu->addAction(tr("(none)"))->setEnabled(false);
    m_recentMenu->addSeparator();
    m_recentMenu->addAction(tr("&Clear Recent Files"), this, [] {
        QSettings().remove(kRecentFilesKey);
    });
}

void MainWindow::reopenClosedTab()
{
    while (!m_closedFiles.isEmpty()) {
        const QString file = m_closedFiles.takeLast();
        if (QFileInfo::exists(file)) {
            openFile(file);
            return;
        }
    }
    showToast(tr("No closed tabs to reopen."));
}

void MainWindow::reloadCurrentTab()
{
    auto *tab = currentTab();
    if (tab && !tab->reload())
        showToast(tr("Could not reload %1").arg(QFileInfo(tab->filePath()).fileName()), 4000);
}

// ---------------------------------------------------------------------------
// Full screen, context menu, preferences, printing, toast

void MainWindow::setFullScreen(bool on)
{
    m_fullScreenAction->setChecked(on);
    if (on == isFullScreen())
        return;
    m_tabRow->setVisible(!on);
    m_toolbar->setVisible(!on);
    if (on) {
        m_wasMaximized = isMaximized();
        showFullScreen();
    } else if (m_wasMaximized) {
        showMaximized();
    } else {
        showNormal();
    }
}

void MainWindow::showTabContextMenu(const QPoint &pos)
{
    const int index = m_tabBar->tabAt(pos);
    DocumentTab *tab = tabAt(index);
    if (!tab)
        return;
    const QString path = tab->filePath();

    QMenu menu(this);
    menu.addAction(tr("Copy &Path"), this, [path] {
        QGuiApplication::clipboard()->setText(QDir::toNativeSeparators(path));
    });
    menu.addAction(tr("Open Containing &Folder"), this, [path] {
#ifdef Q_OS_WIN
        // Opens Explorer with the file selected.
        QProcess::startDetached(QStringLiteral("explorer.exe"),
                                {QStringLiteral("/select,"), QDir::toNativeSeparators(path)});
#else
        QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
#endif
    });
    menu.addSeparator();
    menu.addAction(tr("&Close"), this, [this, tab] {
        closeTab(indexOfTab(tab));
    });
    QAction *closeOthers = menu.addAction(tr("Close &Other Tabs"), this, [this, tab] {
        for (int i = tabCount() - 1; i >= 0; --i) {
            if (tabAt(i) != tab)
                closeTab(i);
        }
    });
    closeOthers->setEnabled(tabCount() > 1);
    QAction *closeRight = menu.addAction(tr("Close Tabs to the &Right"), this, [this, tab] {
        const int keep = indexOfTab(tab);
        for (int i = tabCount() - 1; i > keep; --i)
            closeTab(i);
    });
    closeRight->setEnabled(index < tabCount() - 1);
    menu.exec(m_tabBar->mapToGlobal(pos));
}

bool MainWindow::autoReloadEnabled() const
{
    return QSettings().value(kAutoReloadKey, true).toBool();
}

void MainWindow::showPreferences()
{
    QDialog dialog(this);
    dialog.setWindowTitle(tr("Preferences"));

    auto *autoReload = new QCheckBox(
        tr("Automatically &reload documents when they change on disk"), &dialog);
    autoReload->setChecked(autoReloadEnabled());

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    auto *layout = new QVBoxLayout(&dialog);
    layout->addWidget(autoReload);
    layout->addStretch();
    layout->addWidget(buttons);

    if (dialog.exec() != QDialog::Accepted)
        return;

    const bool enabled = autoReload->isChecked();
    QSettings().setValue(kAutoReloadKey, enabled);
    for (int i = 0; i < tabCount(); ++i)
        tabAt(i)->setAutoReload(enabled);
}

void MainWindow::printDocument()
{
    auto *tab = currentTab();
    if (!tab)
        return;
    QPdfDocument *doc = tab->document();
    const int pageCount = doc->pageCount();

    QPrinter printer(QPrinter::HighResolution);
    printer.setDocName(QFileInfo(tab->filePath()).fileName());
    QPrintDialog dialog(&printer, this);
    dialog.setMinMax(1, pageCount);
    dialog.setOptions(QAbstractPrintDialog::PrintPageRange
                      | QAbstractPrintDialog::PrintCurrentPage
                      | QAbstractPrintDialog::PrintShowPageSize);
    if (dialog.exec() != QDialog::Accepted)
        return;

    int from = 1;
    int to = pageCount;
    if (printer.printRange() == QPrinter::PageRange) {
        from = printer.fromPage();
        to = printer.toPage();
    } else if (printer.printRange() == QPrinter::CurrentPage) {
        from = to = tab->currentPage() + 1;
    }
    from = qBound(1, from, pageCount);
    to = qBound(from, to, pageCount);

    QPainter painter;
    if (!painter.begin(&printer)) {
        showToast(tr("Could not start printing."), 4000);
        return;
    }

    QProgressDialog progress(tr("Printing..."), tr("Cancel"), 0, to - from + 1, this);
    progress.setWindowModality(Qt::WindowModal);
    progress.setMinimumDuration(500);

    // Render each page as an image at up to 300 dpi, scaled to fit the
    // printable area; landscape pages on portrait paper (or vice versa)
    // are rotated to fit, as other readers do.
    const qreal dpi = qMin<qreal>(printer.resolution(), kMaxPrintDpi);
    QPdfDocumentRenderOptions options;
    options.setRenderFlags(QPdfDocumentRenderOptions::RenderFlag::Annotations);
    bool canceled = false;
    for (int page = from; page <= to; ++page) {
        progress.setValue(page - from);
        if (progress.wasCanceled()) {
            canceled = true;
            printer.abort();
            break;
        }
        if (page != from)
            printer.newPage();

        const QRect area = painter.viewport();
        const QSizeF points = doc->pagePointSize(page - 1);
        const bool rotate = (points.width() > points.height())
                         != (area.width() > area.height());
        const QSizeF oriented = rotate ? points.transposed() : points;
        QSizeF target = oriented.scaled(QSizeF(area.size()), Qt::KeepAspectRatio);
        if (rotate)
            target.transpose();

        const QImage image = doc->render(page - 1, (points * dpi / 72.0).toSize(), options);
        painter.save();
        painter.translate(QRectF(area).center());
        if (rotate)
            painter.rotate(90);
        painter.drawImage(QRectF(-target.width() / 2, -target.height() / 2,
                                 target.width(), target.height()),
                          image);
        painter.restore();
    }
    painter.end();
    progress.setValue(to - from + 1);
    if (!canceled)
        showToast(tr("Sent %n page(s) to the printer.", nullptr, to - from + 1));
}

void MainWindow::showToast(const QString &message, int msecs)
{
    m_toast->setText(message);
    m_toast->adjustSize();
    positionToast();
    m_toast->show();
    m_toast->raise();
    m_toastTimer->start(msecs);
}

void MainWindow::positionToast()
{
    if (!m_toast)
        return;
    const QRect area = centralWidget()->geometry();
    m_toast->move(area.center().x() - m_toast->width() / 2,
                  area.bottom() - m_toast->height() - 24);
}

// ---------------------------------------------------------------------------
// Events

void MainWindow::closeEvent(QCloseEvent *event)
{
    // Never persist full-screen state or its hidden toolbars.
    setFullScreen(false);
    for (int i = 0; i < tabCount(); ++i)
        rememberReadingPosition(tabAt(i));
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    settings.setValue(QStringLiteral("window/state"), saveState());
    QMainWindow::closeEvent(event);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        if (isFullScreen()) {
            setFullScreen(false);
            return;
        }
        if (auto *tab = currentTab()) {
            tab->view()->clearSelection();
            return;
        }
    }
    QMainWindow::keyPressEvent(event);
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    positionToast();
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
