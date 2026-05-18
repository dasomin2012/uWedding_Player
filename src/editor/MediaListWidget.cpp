#include "MediaListWidget.h"

#include "app/Settings.h"
#include "player/SnapshotCache.h"
#include "scene/Layer.h"

#include <QListWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QFileInfo>
#include <QDir>
#include <QMimeData>
#include <QPixmap>
#include <QIcon>

namespace uwp {

static const char* kMediaMime = "application/x-uwp-media-path";

// 드래그 시 커스텀 MIME(파일 경로) 제공.
class MediaList : public QListWidget {
public:
    using QListWidget::QListWidget;
protected:
    // Qt 5.15 시그니처: 값 전달 (Qt6 는 const ref)
    QMimeData* mimeData(const QList<QListWidgetItem*> items) const override {
        auto* md = new QMimeData;
        if (!items.isEmpty()) {
            const QString p = items.first()->data(Qt::UserRole).toString();
            md->setData(kMediaMime, p.toUtf8());
            md->setUrls({ QUrl::fromLocalFile(p) });
        }
        return md;
    }
};

MediaListWidget::MediaListWidget(Settings* settings, SnapshotCache* snapshots,
                                 QWidget* parent)
    : QWidget(parent)
    , m_settings(settings)
    , m_snapshots(snapshots)
{
    auto* title = new QLabel("Media Files");
    title->setStyleSheet("font-weight: bold;");

    auto* btnFile   = new QPushButton("Open File…");
    auto* btnFolder = new QPushButton("Open Folder…");
    auto* btnRow    = new QHBoxLayout;
    btnRow->setContentsMargins(0, 0, 0, 0);
    btnRow->addWidget(btnFile);
    btnRow->addWidget(btnFolder);

    m_list = new MediaList(this);
    m_list->setIconSize(QSize(96, 54));
    m_list->setDragEnabled(true);
    m_list->setDragDropMode(QAbstractItemView::DragOnly);
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setUniformItemSizes(false);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->addWidget(title);
    lay->addLayout(btnRow);
    lay->addWidget(m_list);

    connect(btnFile,   &QPushButton::clicked, this, &MediaListWidget::openFile);
    connect(btnFolder, &QPushButton::clicked, this, &MediaListWidget::openFolder);
    connect(m_list, &QListWidget::itemDoubleClicked,
            this, &MediaListWidget::onItemDoubleClicked);
    if (m_snapshots) {
        connect(m_snapshots, &SnapshotCache::snapshotReady,
                this, &MediaListWidget::onSnapshotReady);
    }
}

bool MediaListWidget::isSupported(const QString& path) const {
    return guessMediaType(path) != MediaType::Unknown;
}

void MediaListWidget::addPath(const QString& path) {
    if (!QFileInfo::exists(path) || !isSupported(path)) return;
    // 중복 방지
    for (int i = 0; i < m_list->count(); ++i)
        if (m_list->item(i)->data(Qt::UserRole).toString() == path) return;

    auto* item = new QListWidgetItem(QFileInfo(path).fileName(), m_list);
    item->setData(Qt::UserRole, path);
    item->setToolTip(path);
    item->setIcon(QIcon());   // 스냅샷 준비 전 빈 아이콘

    if (m_snapshots) {
        const QImage c = m_snapshots->cached(path);
        if (!c.isNull()) item->setIcon(QPixmap::fromImage(c));
        else             m_snapshots->request(path);
    }
}

void MediaListWidget::openFile() {
    const QString start = m_settings ? m_settings->mediaDir() : QString();
    const QStringList files = QFileDialog::getOpenFileNames(
        this, "Open Media File(s)", start,
        "Media (*.mp4 *.mov *.avi *.mkv *.wmv *.png *.jpg *.jpeg *.bmp "
        "*.gif *.webp *.pdf);;All files (*.*)");
    if (files.isEmpty()) return;
    for (const QString& f : files) addPath(f);
    if (m_settings) {
        m_settings->setMediaDir(QFileInfo(files.first()).absolutePath());
    }
}

void MediaListWidget::openFolder() {
    const QString start = m_settings ? m_settings->mediaDir() : QString();
    const QString dir = QFileDialog::getExistingDirectory(
        this, "Open Media Folder", start);
    if (dir.isEmpty()) return;

    const QStringList filters = {
        "*.mp4","*.mov","*.avi","*.mkv","*.wmv","*.m4v","*.webm",
        "*.png","*.jpg","*.jpeg","*.bmp","*.gif","*.webp","*.tif","*.tiff",
        "*.pdf","*.ppt","*.pptx"
    };
    const QFileInfoList entries =
        QDir(dir).entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo& fi : entries) addPath(fi.absoluteFilePath());
    if (m_settings) m_settings->setMediaDir(dir);
}

void MediaListWidget::onItemDoubleClicked(QListWidgetItem* item) {
    if (item) emit mediaActivated(item->data(Qt::UserRole).toString());
}

void MediaListWidget::onSnapshotReady(const QString& mediaPath,
                                      const QImage& image) {
    for (int i = 0; i < m_list->count(); ++i) {
        QListWidgetItem* it = m_list->item(i);
        if (it->data(Qt::UserRole).toString() == mediaPath)
            it->setIcon(QPixmap::fromImage(image));
    }
}

} // namespace uwp
