#pragma once

#include <QWidget>

class QLabel;
class QLineEdit;
class QPdfDocument;
class QPdfSearchModel;
class QPdfView;

// One open PDF: a QPdfView plus its document, search model, and a
// hideable in-tab search bar.
class DocumentTab : public QWidget
{
    Q_OBJECT

public:
    explicit DocumentTab(const QString &filePath, QWidget *parent = nullptr);

    bool isValid() const { return m_valid; }
    QString filePath() const { return m_filePath; }
    QPdfDocument *document() const { return m_document; }
    QPdfView *view() const { return m_view; }

    int currentPage() const;
    void goToPage(int page); // 0-based

    void showSearchBar();

signals:
    void currentPageChanged(int page); // 0-based

private:
    void findNext();
    void findPrevious();
    void jumpToResult(int index);
    void updateSearchLabel();

    QString m_filePath;
    bool m_valid = false;

    QPdfDocument *m_document = nullptr;
    QPdfView *m_view = nullptr;
    QPdfSearchModel *m_searchModel = nullptr;
    int m_currentResult = -1;

    QWidget *m_searchBar = nullptr;
    QLineEdit *m_searchEdit = nullptr;
    QLabel *m_searchCountLabel = nullptr;
};
