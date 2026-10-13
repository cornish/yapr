#include "ThumbnailModel.h"

#include <QGuiApplication>
#include <QPdfDocument>
#include <QPdfPageRenderer>
#include <QScreen>

ThumbnailModel::ThumbnailModel(QObject *parent)
    : QAbstractListModel(parent)
{
    m_renderer = new QPdfPageRenderer(this);
    m_renderer->setRenderMode(QPdfPageRenderer::RenderMode::MultiThreaded);

    connect(m_renderer, &QPdfPageRenderer::pageRendered, this,
            [this](int pageNumber, QSize, const QImage &image,
                   QPdfDocumentRenderOptions, quint64) {
                QPixmap pixmap = QPixmap::fromImage(image);
                pixmap.setDevicePixelRatio(
                    qGuiApp->primaryScreen()->devicePixelRatio());
                m_cache.insert(pageNumber, pixmap);
                const QModelIndex idx = index(pageNumber);
                emit dataChanged(idx, idx, {Qt::DecorationRole});
            });
}

void ThumbnailModel::setDocument(QPdfDocument *document)
{
    beginResetModel();
    m_document = document;
    m_cache.clear();
    m_requested.clear();
    m_renderer->setDocument(document);
    endResetModel();
}

int ThumbnailModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_document)
        return 0;
    return m_document->pageCount();
}

QVariant ThumbnailModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_document)
        return {};
    const int page = index.row();

    switch (role) {
    case Qt::DisplayRole:
        return QString::number(page + 1);
    case Qt::DecorationRole: {
        const auto it = m_cache.constFind(page);
        if (it != m_cache.constEnd())
            return *it;
        if (!m_requested.contains(page)) {
            m_requested.insert(page);
            const_cast<QPdfPageRenderer *>(m_renderer)
                ->requestPage(page, thumbnailSize(page));
        }
        // Placeholder keeps item geometry stable until the render lands.
        QPixmap blank(thumbnailSize(page));
        blank.setDevicePixelRatio(qGuiApp->primaryScreen()->devicePixelRatio());
        blank.fill(Qt::white);
        return blank;
    }
    default:
        return {};
    }
}

QSize ThumbnailModel::thumbnailSize(int page) const
{
    const QSizeF pageSize = m_document->pagePointSize(page);
    const qreal dpr = qGuiApp->primaryScreen()->devicePixelRatio();
    const qreal width = kThumbWidth * dpr;
    if (pageSize.width() <= 0)
        return QSize(int(width), int(width * 1.41));
    return QSize(int(width), int(width * pageSize.height() / pageSize.width()));
}
