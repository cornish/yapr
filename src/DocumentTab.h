#pragma once

#include <QGraphicsEffect>
#include <QWidget>

class QLabel;
class QLineEdit;
class QPdfDocument;
class QPdfSearchModel;
class QPdfView;

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
    void setInvertedColors(bool inverted);

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
    InvertEffect *m_invertEffect = nullptr;
};
