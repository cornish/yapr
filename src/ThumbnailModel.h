#pragma once

#include <QAbstractListModel>
#include <QHash>
#include <QPixmap>
#include <QSet>

class QPdfDocument;
class QPdfPageRenderer;

// Lazy, asynchronously rendered page thumbnails for the sidebar.
// Pages render on demand (when the view first asks for them) on a
// worker thread, so a 1000-page document costs nothing until scrolled.
class ThumbnailModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit ThumbnailModel(QObject *parent = nullptr);

    void setDocument(QPdfDocument *document);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;

    static constexpr int kThumbWidth = 140;

private:
    QSize thumbnailSize(int page) const;

    QPdfDocument *m_document = nullptr;
    QPdfPageRenderer *m_renderer = nullptr;
    QHash<int, QPixmap> m_cache;
    mutable QSet<int> m_requested;
};
