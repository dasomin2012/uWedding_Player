#pragma once

#include <QWidget>
#include <QString>

class QListWidget;
class QListWidgetItem;

namespace uwp {

class Settings;
class SnapshotCache;

// 미디어 소스 패널.
//  - "Open File…"  : 임의 파일 1개+ 선택 → 목록에 추가
//  - "Open Folder…": 폴더 내 미디어 파일 일괄 목록 표시
//  - 항목: SnapshotCache 썸네일 + 파일명
//  - 드래그 소스(커스텀 MIME) → PreviewCanvas 드롭
//  - 더블클릭 → mediaActivated(path)
class MediaListWidget : public QWidget {
    Q_OBJECT
public:
    MediaListWidget(Settings* settings, SnapshotCache* snapshots,
                    QWidget* parent = nullptr);

signals:
    void mediaActivated(const QString& path);   // 더블클릭

private slots:
    void openFile();
    void openFolder();
    void onItemDoubleClicked(QListWidgetItem* item);
    void onSnapshotReady(const QString& mediaPath, const QImage& image);

private:
    void addPath(const QString& path);
    bool isSupported(const QString& path) const;

    Settings*      m_settings  = nullptr;
    SnapshotCache* m_snapshots = nullptr;
    QListWidget*   m_list      = nullptr;
};

} // namespace uwp
