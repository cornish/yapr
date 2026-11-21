#pragma once

#include <QMainWindow>

class DocumentTab;
class QLabel;
class QLineEdit;
class QListView;
class QMenu;
class QPdfBookmarkModel;
class QSpinBox;
class QStackedWidget;
class QTabBar;
class QTimer;
class QToolBar;
class QTreeView;
class ThumbnailModel;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

    // Opens a file in a new tab, or switches to its tab if already open.
    void openFile(const QString &filePath);
    void restoreSession();

protected:
    void closeEvent(QCloseEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void createActions();
    void createMenu();
    void createTabRow();
    void createToolbar();
    void createSidebar();

    // Tabs: a QTabBar (in the top row) plus a QStackedWidget, linked through
    // tab data so the bar can be reordered freely.
    int tabCount() const;
    DocumentTab *tabAt(int index) const;
    int indexOfTab(DocumentTab *tab) const;
    DocumentTab *currentTab() const;

    void openFileDialog();
    void closeTab(int index);
    void rememberReadingPosition(DocumentTab *tab);
    void currentTabChanged(int index);
    void setZoomMode(int mode);
    void zoomBy(qreal factor);
    void syncPageControls();
    void syncZoomActions();
    void syncHistoryActions();
    void syncSearchControls();
    void saveSession();
    void addRecentFile(const QString &filePath);
    void rebuildRecentMenu();
    void reopenClosedTab();
    void reloadCurrentTab();
    void goToPageDialog();
    void focusFind();
    void setFullScreen(bool on);
    void showTabContextMenu(const QPoint &pos);
    void showPreferences();
    void printDocument();
    bool autoReloadEnabled() const;
    void showToast(const QString &message, int msecs = 2500);
    void positionToast();

    QTabBar *m_tabBar = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_emptyPage = nullptr;
    QWidget *m_tabRow = nullptr;
    QToolBar *m_toolbar = nullptr;
    QMenu *m_mainMenu = nullptr;
    QMenu *m_recentMenu = nullptr;

    QSpinBox *m_pageSpin = nullptr;
    QLabel *m_pageCountLabel = nullptr;
    QLineEdit *m_findEdit = nullptr;
    QLabel *m_findStatusLabel = nullptr;

    QLabel *m_toast = nullptr;
    QTimer *m_toastTimer = nullptr;

    QTreeView *m_outlineView = nullptr;
    QPdfBookmarkModel *m_bookmarkModel = nullptr;
    QListView *m_thumbnailView = nullptr;
    ThumbnailModel *m_thumbnailModel = nullptr;
    QAction *m_sidebarAction = nullptr;

    QAction *m_openAction = nullptr;
    QAction *m_printAction = nullptr;
    QAction *m_closeTabAction = nullptr;
    QAction *m_reopenAction = nullptr;
    QAction *m_quitAction = nullptr;
    QAction *m_copyAction = nullptr;
    QAction *m_selectAllAction = nullptr;
    QAction *m_findAction = nullptr;
    QAction *m_findNextAction = nullptr;
    QAction *m_findPrevAction = nullptr;
    QAction *m_prevPageAction = nullptr;
    QAction *m_nextPageAction = nullptr;
    QAction *m_backAction = nullptr;
    QAction *m_forwardAction = nullptr;
    QAction *m_goToPageAction = nullptr;
    QAction *m_fitWidthAction = nullptr;
    QAction *m_fitPageAction = nullptr;
    QAction *m_actualSizeAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
    QAction *m_fullScreenAction = nullptr;
    QAction *m_reloadAction = nullptr;
    QAction *m_invertAction = nullptr;
    QAction *m_themeToggleAction = nullptr;
    QAction *m_preferencesAction = nullptr;

    // Tabs closed this run, most recent last. Deliberately in-memory only:
    // reopening is something you ask for, never something that happens.
    QStringList m_closedFiles;
    bool m_wasMaximized = false;
    bool m_restoringSession = false;
};
