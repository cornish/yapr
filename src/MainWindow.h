#pragma once

#include <QMainWindow>

class DocumentTab;
class QComboBox;
class QLabel;
class QPdfBookmarkModel;
class QSpinBox;
class QTabWidget;
class QTreeView;

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

private:
    void openFileDialog();
    void closeTab(int index);
    void currentTabChanged(int index);
    void applyZoomSelection(int comboIndex);
    void zoomBy(qreal factor);
    void syncPageControls();
    void saveSession();
    DocumentTab *currentTab() const;

    QTabWidget *m_tabs = nullptr;

    QSpinBox *m_pageSpin = nullptr;
    QLabel *m_pageCountLabel = nullptr;
    QComboBox *m_zoomCombo = nullptr;

    QTreeView *m_outlineView = nullptr;
    QPdfBookmarkModel *m_bookmarkModel = nullptr;

    bool m_restoringSession = false;
};
