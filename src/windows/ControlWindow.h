#pragma once

#include <QMainWindow>
#include <QString>

class QLabel;
class QWidget;
class QPushButton;
class QAction;
class QImage;

namespace uwp {

class Settings;
class SceneModel;
class SnapshotCache;
class PreviewCanvas;
class PropertyPanel;
class MediaListWidget;
class ProgramListWidget;

// Phase 3 운용자 화면.
//   +-----------+----------------------+----------+
//   | MediaList | PreviewCanvas | Take | Property |
//   |  (편집)   |   (자유레이어) | Mir. |  Panel   |
//   |           +----------------------+          |
//   |           | Program List (8)     |          |
//   +-----------+----------------------+----------+
class ControlWindow : public QMainWindow {
    Q_OBJECT
public:
    ControlWindow(Settings* settings, SceneModel* scene,
                  SnapshotCache* snapshots, QWidget* parent = nullptr);
    ~ControlWindow() override;

public slots:
    void setStatusText(const QString& text);

    // LiveMirror: Application 이 폴링한 프레임을 우상단 "Live 송출" 패널로.
    // 빈 QImage 는 무시(직전 프레임 유지).
    void setLiveMirrorImage(const QImage& img);

    // UI-F: Preview 툴바 시간 표시.
    //  seconds < 0 : 프로그램 없음 → "표시 시간: —"
    //  seconds = 0 : 자동 진행 없음 → "표시 시간: 수동"
    //  seconds > 0 : "표시 시간: MM:SS"
    void setPreviewDisplayTime(int seconds);

public:
    ProgramListWidget* programList()  const { return m_programList; }  // Application 와이어링용
    PreviewCanvas*     previewCanvas() const { return m_canvas; }      // 썸네일 렌더용

signals:
    void selectOutputMonitorRequested();
    void openSettingsRequested();
    void playTestVideoRequested();
    void saveSceneRequested();
    void loadSceneRequested();
    void takeRequested();
    void takeModeChanged(const QString& mode);   // "cut" | "fade"

private:
    void createMenus();
    void createCentralLayout();
    QWidget* buildLivePanel();              // Live mirror + TAKE 클러스터
    QWidget* buildPreviewPane();            // UI-F: 툴바 + PreviewCanvas
    void applyTheme(const QString& theme);   // "light" | "dark" — QSS 적용
    void toggleTheme();                      // 다크 모드 토글 (Tools 메뉴)

    Settings*        m_settings   = nullptr;
    SceneModel*      m_scene      = nullptr;
    SnapshotCache*   m_snapshots  = nullptr;

    MediaListWidget* m_mediaList  = nullptr;
    PreviewCanvas*   m_canvas     = nullptr;
    PropertyPanel*   m_property   = nullptr;
    QLabel*            m_liveMirror = nullptr;
    ProgramListWidget* m_programList = nullptr;

    // UI-C: TAKE 클러스터 (Live mirror 아래)
    //  검정 화면은 별도 버튼 없이 "빈 프로그램(레이어 0개)"을 TAKE 로 실행 —
    //  applyScene({}) 가 LiveWindow 검정 배경을 그대로 보여준다.
    QPushButton*  m_takeButton      = nullptr;   // TAKE (큰 빨간 버튼)
    QPushButton*  m_btnTransition   = nullptr;   // Fade ↔ Cut 토글 (한 버튼)

    // UI-F: Preview 인라인 툴바
    //   + 레이어 버튼은 미디어 라이브러리 드래그앤드롭과 기능 중복이라 제거.
    QPushButton* m_btnPreviewPlay = nullptr;   // ▶ (placeholder)
    QLabel*      m_timeLabel      = nullptr;   // 표시 시간
    QPushButton* m_btnFillCanvas  = nullptr;   // 우측: 선택 레이어 캔버스에 꽉 채우기
    QPushButton* m_btnDeleteLayer = nullptr;   // 우측 끝: 선택 레이어 삭제(휴지통)

    // UI-B: 다크 모드
    QAction*     m_darkThemeAct = nullptr;
    QString      m_currentTheme = "light";
};

} // namespace uwp
