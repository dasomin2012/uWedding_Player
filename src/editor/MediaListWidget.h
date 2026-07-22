#pragma once

#include <QWidget>
#include <QString>
#include <QStringList>
#include <QPoint>

class QListWidget;
class QListWidgetItem;
class QPushButton;
class QButtonGroup;

namespace uwp {

class Settings;
class SnapshotCache;

// 미디어 라이브러리 패널.
//  - "파일 추가" / "폴더 추가" : 소스 추가 (Settings.media_sources 에 영속)
//  - 시작 시 이전 소스 자동 복원 (폴더는 재스캔)
//  - 타입 탭: [전체 | 영상 | 이미지] — 필터
//  - 타일 그리드: 큰 썸네일(128×72) + 파일명 (SnapshotCache 결과 자동 표시)
//  - 드래그 소스(커스텀 MIME) → PreviewCanvas 드롭
//  - 더블클릭 → mediaActivated(path)
//  - 카드 hover/선택 시 우상단 X → 소스(파일 또는 폴더) 단위 제거
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
    void applyFilter();                          // 검색/타입 필터 재적용

private:
    // path = 표시할 미디어 파일, source = 그 항목을 만든 소스(파일 자신 또는 폴더)
    void addPath(const QString& path, const QString& source);
    bool isSupported(const QString& path) const;
    QStringList scanFolder(const QString& dir) const;  // 폴더 내 지원 미디어
    void rebuildList();          // m_sources 로부터 목록 전체 재구성
    void persistSources();       // m_sources → Settings 저장
    // 카드 X 버튼 → 그 카드의 소스(파일 또는 폴더) 제거. 폴더면 확인 모달.
    void removeSourceAt(int row);

    Settings*      m_settings  = nullptr;
    SnapshotCache* m_snapshots = nullptr;
    QListWidget*   m_list      = nullptr;
    QStringList    m_sources;    // 기억된 소스(파일/폴더 경로), 추가 순서

    // UI-E: 타입 탭 필터
    QButtonGroup*  m_typeGroup   = nullptr;   // 3개 checkable 버튼(exclusive)
    int            m_typeFilter  = -1;        // -1 = 전체, 그 외 = static_cast<int>(MediaType)
};

} // namespace uwp
