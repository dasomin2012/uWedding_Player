#pragma once

#include <QMainWindow>
#include <QString>

class QLabel;
class QWidget;
class QPushButton;
class QAction;
class QImage;
class QTimer;

namespace uwp {

class Settings;
class SceneModel;
class SnapshotCache;
class PreviewCanvas;
class PropertyPanel;
class MediaListWidget;
class ProgramListWidget;
class PageListWidget;

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

    // 외부(예: 리허설 창 close event)에서 미리보기 상태를 강제 리셋.
    void resetPreviewSim();

    // 하단 [프로그램][페이지] 탭에서 페이지 탭을 활성화 (프로그램 로드 후 호출).
    void showPagesTab();
    void showProgramsTab();

    // 응급 BLACK 버튼 시각 상태 — Application 이 실제 상태 관리.
    void setBlackoutActive(bool active);

    // 리허설 체인 (Loop/Next/First) 연속 재생 시 카운트다운만 재시작.
    //   newTotalSec: 다음 프로그램의 displayTimeSec (0 = 새 프로그램이 수동
    //   진행 — 카운트다운 정지, 창은 유지). 창 열림/닫힘 상태는 건드리지 않음.
    void restartPreviewCountdown(int newTotalSec);

public:
    ProgramListWidget* programList()  const { return m_programList; }  // Application 와이어링용
    PageListWidget*    pageList()     const { return m_pageList; }     // UI-D Phase B
    PreviewCanvas*     previewCanvas() const { return m_canvas; }      // 썸네일 렌더용

signals:
    void selectOutputMonitorRequested();
    void openSettingsRequested();
    void playTestVideoRequested();
    void saveSceneRequested();
    void loadSceneRequested();
    void takeRequested();
    void takeModeChanged(const QString& mode);   // "cut" | "fade"
    // 미리보기 재생 상태 변화 — Application 이 리허설 창을 열고/닫는 트리거.
    void previewPlayingChanged(bool playing);
    // 카운트다운이 자연 만료 (auto-complete). Application 이 프로그램의
    // endAction 을 조회해 체인(반복/다음/첫)·정지·유지 중 하나를 실행.
    void previewCompleted();
    // 응급 F2B 토글. Application 이 상태 관리 + setBlackoutActive 로 시각 회신.
    void blackoutRequested();

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
    PageListWidget*  m_pageList   = nullptr;   // 하단 [페이지] 탭 (Phase B → 3안 재배치)
    PreviewCanvas*   m_canvas     = nullptr;
    PropertyPanel*   m_property   = nullptr;
    QLabel*            m_liveMirror  = nullptr;
    ProgramListWidget* m_programList = nullptr;

    // UI-C: TAKE 클러스터 (Live mirror 아래)
    //  검정 화면은 별도 버튼 없이 "빈 프로그램(레이어 0개)"을 TAKE 로 실행 —
    //  applyScene({}) 가 LiveWindow 검정 배경을 그대로 보여준다.
    QPushButton*  m_takeButton      = nullptr;   // TAKE (큰 빨간 버튼)
    QPushButton*  m_btnTransition   = nullptr;   // Fade ↔ Cut 토글 (한 버튼)
    QPushButton*  m_btnBlack        = nullptr;   // 응급 F2B 토글 (BLACK)

    // UX-1: 상태표시줄 위젯 (색 도트 + 엔진/모니터 라벨 + 시계).
    QLabel*  m_statusObsDot      = nullptr;
    QLabel*  m_statusObsText     = nullptr;
    QLabel*  m_statusMonitorDot  = nullptr;
    QLabel*  m_statusMonitorText = nullptr;
    QLabel*  m_statusClock       = nullptr;
    QTimer*  m_clockTimer        = nullptr;

    // UX-1: 툴바 우측 모드 토글 (UX-4 에서 활성화 예정 — 지금은 자리만).
    QPushButton* m_btnModePrep   = nullptr;
    QPushButton* m_btnModeShow   = nullptr;

    // UX-1: 상태표시줄 초기화 및 시계 tick.
    void setupStatusBar();
    void updateClock();

    // UI-F: Preview 인라인 툴바
    //   + 레이어 버튼은 미디어 라이브러리 드래그앤드롭과 기능 중복이라 제거.
    QPushButton* m_btnPreviewPlay = nullptr;   // ▶ / ⏸ 카운트다운 토글
    QLabel*      m_timeLabel      = nullptr;   // 표시 시간
    QPushButton* m_btnFillCanvas  = nullptr;   // 우측: 선택 레이어 캔버스에 꽉 채우기
    QPushButton* m_btnDeleteLayer = nullptr;   // 우측 끝: 선택 레이어 삭제(휴지통)

    // 미리보기 카운트다운 시뮬레이션 상태.
    //   Preview 는 정지화상 원칙 유지 — 실제 재생 대신 displayTimeSec 이
    //   흐를 시간을 시각화하여 편집자가 "이 프로그램이 화면에 얼마나 머무는가"
    //   를 실시간 감각으로 확인.
    QTimer* m_previewTimer     = nullptr;
    int     m_previewTotalSec  = -1;   // <0=프로그램 없음, 0=수동, >0=자동
    int     m_previewElapsedMs = 0;
    bool    m_previewRunning   = false;

    void togglePreviewPlay();          // ▶/⏸ 클릭
    void tickPreviewPlay();            // 100ms 타이머
    // resetPreviewSim 은 public slots 로 이동 (외부에서도 강제 리셋 가능)
    void updateTimeLabel();            // 상태 기반 라벨 재작성

    // UI-B: 다크 모드
    QAction*     m_darkThemeAct = nullptr;
    QString      m_currentTheme = "light";
};

} // namespace uwp
