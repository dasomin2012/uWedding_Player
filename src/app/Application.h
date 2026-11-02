#pragma once

#include <QObject>
#include <QString>
#include <memory>

#include "Settings.h"

class QScreen;
class QSystemTrayIcon;
class QTimer;

namespace uwp {

// 두 번째 실행 → 기존 인스턴스에 "제어 창 열기" 요청 (Win32 등록 메시지 이름).
//   main.cpp 가 HWND_BROADCAST 로 보내고, Application 의 네이티브 필터가 수신.
inline constexpr char kShowControlMessage[] = "Hanmac-uWeddingPlayer-ShowControl";

class ControlWindow;
class LiveWindow;
class LivePlayerPool;
class SnapshotCache;
class SceneModel;
class TakeController;
class NovaStarController;
class ProgramRepository;
class AudioPlayer;
struct Program;   // UI-D Phase C — playPageAt 시그니처가 사용
struct Page;
class ILiveSink;
#if defined(UWP_HAS_OBS)
class ObsProcessManager;
class ObsLiveBackend;
#endif

// 두 윈도우(Control, Live)의 라이프타임과 Settings I/O 를 묶는 코디네이터.
class Application : public QObject {
    Q_OBJECT
public:
    explicit Application(QObject* parent = nullptr);
    ~Application() override;

    bool initialize();
    void shutdown();

    Settings& settings() { return m_settings; }

protected:
    // 리허설 창의 close event(X 클릭) 를 감지해 ControlWindow 상태와 동기화.
    bool eventFilter(QObject* obj, QEvent* event) override;

private slots:
    void onSelectOutputMonitorRequested();
    void onDisplaySettingsRequested();      // 캔버스 해상도 + 모니터 통합
    void onOpenSettingsRequested();
    void onPlayTestVideoRequested();
    void onSaveSceneRequested();
    void onLoadSceneRequested();

    // Phase 5b — Program 리스트 동작
    void onProgramAddRequested();
    void onProgramSelected(const QString& id);                 // 단일클릭: Preview 로드
    void onProgramPlayRequested(const QString& id);            // 더블클릭/Play: Take
    void onProgramRenameRequested(const QString& id, const QString& newName);
    void onProgramDeleteRequested(const QString& id);
    void onProgramEndActionEditRequested(const QString& id);

    // UI-D Phase B — 페이지 관리 (현재 편집 대상 프로그램 안).
    void onPageAddRequested();
    void onPageSelected(const QString& pageId);
    void onPageDeleteRequested(const QString& pageId);
    void onPageRenameRequested(const QString& pageId, const QString& newName);
    void onPageMoveUpRequested(const QString& pageId);
    void onPageMoveDownRequested(const QString& pageId);
    // UI-D Phase C — 페이지별 표시 시간 편집.
    void onPageDisplayTimeEditRequested(const QString& pageId);

    // ▶ 미리보기 재생 상태 변화 → 리허설 창 열기/닫기.
    void onPreviewPlayingChanged(bool playing);
    // 리허설 카운트다운 자연 만료 → 프로그램의 endAction 조회하여 체인 결정.
    void onPreviewCompleted();
    // 응급 BLACK 토글 (Live 검정 / 복귀).
    void onBlackoutRequested();

    // 클러스터 ▶/⏸ 토글 — 컨텍스트별 재생/일시정지/재개.
    //   대기 → 편집중 프로그램 첫 페이지부터 재생 (m_editProgramId 필요)
    //   재생중 → 일시정지 (자동 진행 타이머 stop, Live 유지)
    //   일시정지 → 재개 (현재 페이지 처음부터 카운트다운 재시작)
    void onPlayPauseRequested();

private:
    QString resolveSettingsPath() const;
    QString resolveScenePath() const;
    QString dataDir() const;                         // <appDir>/data
    QString resolveProgramsPath() const;             // <appDir>/data/programs.json

    // ProgramProperties 앞/뒤로 버튼 → 편집중 프로그램의 위치를 이동.
    // delta = -1 (앞으로) / +1 (뒤로).
    void moveEditProgram(int delta);

    // Phase 5b — 편집 자동저장 (현재 편집 대상 program 으로 binding)
    void scheduleEditSave();                         // 씬 변경 → 디바운스 타이머 재시작
    void persistEditProgram();                       // 편집 대상에 layers+썸네일 저장
    void persistSessionState();                      // 마지막 편집 프로그램 id 를 QSettings 에
    void restoreSessionState();                      // 앱 시작 시 세션 복원
    void flushEditSave();                            // 대기중 저장을 즉시 반영

    // Phase 5c — program 재생/자동 진행 (UI-D: 페이지 단위로 확장됨)
    void playProgram(const QString& id);             // 첫 페이지부터 재생
    void onProgramAdvance();                         // 페이지 만료 → 다음 페이지 or program endAction
    void stopProgramPlayback();                      // Stop: Live 클리어 + 타이머 정지
    // 현재 재생 프로그램의 특정 페이지를 Live 로 송출 + 타이머 무장.
    void playPageAt(const Program* p, int pageIdx);
    // 페이지 표시 시간 타이머 무장. 지정 초면 즉시, 재생 길이면 대표 영상
    //   길이가 폴러로 확인될 때 무장(m_waitVideoEnd).
    void armPageAdvance(const Page& pg);
    QString currentNovaPresetId() const;            // O5: env > settings.default_preset_id

    // 송출 창(qt LiveWindow) 을 설정(monitor/screen)대로 표시.
    void showLiveWindow();
    // 송출화면 보이기/숨기기 실제 반영 — qt: LiveWindow hide/show (바탕화면 노출),
    //   obs: 프로젝터 창 닫기/열기.
    void applyOutputHidden(bool hidden);
    // 송출 화면이 놓일 모니터 (설정 기준 추정). 없으면 nullptr.
    QScreen* liveOutputScreen() const;
    // 다중 모니터에서 제어 창이 송출 모니터에 뜨지 않도록 다른 모니터로 이동.
    void placeControlAwayFromLive();
    // 제어 창 X — 송출화면 종료(앱 종료) / 유지(트레이 상주) / 취소. true=종료.
    bool confirmControlClose();
    void showControlWindow();                       // 트레이·재실행에서 제어 창 복귀
    void ensureTrayIcon();
#if defined(UWP_HAS_OBS)
    void installQtFallback(const QString& reason);  // O6-A: OBS Failed → qt
#endif

    Settings                        m_settings;
    QString                         m_settingsPath;
    std::unique_ptr<LivePlayerPool> m_playerPool;     // 윈도우보다 오래 살아야 함
    std::unique_ptr<SnapshotCache>  m_snapshotCache;
    std::unique_ptr<SceneModel>     m_scene;
    std::unique_ptr<ProgramRepository> m_programs;     // Phase 5
    std::unique_ptr<ControlWindow>  m_controlWindow;
    std::unique_ptr<LiveWindow>     m_liveWindow;
    // 두 번째 LiveWindow 인스턴스 — Preview 리허설(Control 모니터에 작은 창).
    // ILiveSink 인 것은 무관; TakeController 는 이 창을 알지 못함.
    std::unique_ptr<LiveWindow>     m_rehearsalWindow;
#if defined(UWP_HAS_OBS)
    // 선언 순서 주의: backend 를 proc 보다 먼저 선언 → proc 가 먼저 소멸
    // (proc 소멸 시 OBS 종료·ObsClient abort, 이때 backend 는 아직 생존).
    std::unique_ptr<ObsLiveBackend>    m_obsBackend;
    std::unique_ptr<ObsProcessManager> m_obsProc;
#endif
    // O5: m_takeController 보다 위에 선언 → take 가 먼저 소멸 후 nova 소멸.
    // taken/transitionEnded 콜백이 nova 참조 → 역순 소멸로 안전 보장.
    std::unique_ptr<NovaStarController> m_novaStar;
    std::unique_ptr<TakeController> m_takeController;

    // O6-A 폴백 후에도 LED 동기가 이어지도록 taken 라우팅 조건에 사용.
    bool m_qtFallbackActive = false;

    // 송출화면 유지로 제어 창을 닫았을 때 상주 아이콘.
    QSystemTrayIcon* m_tray = nullptr;

    // Phase 5b/5c — 현재 재생중(Play) program id ("" = 없음).
    QString m_currentProgramId;
    // UI-D Phase C — 재생중 프로그램 내 현재 페이지 인덱스.
    int     m_currentPageIdx = 0;

    // Phase 5b — 현재 편집 대상 program id ("" = 스크래치, 자동저장 안 함).
    QString m_editProgramId;
    // UI-D Phase B — 편집 대상 프로그램 내 현재 편집 페이지 id.
    //   프로그램이 로드되면 pages.first().id 로 초기화.
    //   페이지 스위치·페이지 삭제·프로그램 언로드 시 갱신.
    QString m_editPageId;

    // 리허설 세션이 지금 재생중인 program id. m_editProgramId 와 분리 —
    // 리허설이 Next/First 로 다른 프로그램 재생해도 사용자 편집 대상은 유지.
    QString m_previewProgramId;
    // UI-D Phase C — 리허설 세션 내 현재 페이지 인덱스.
    int     m_previewPageIdx = 0;

    // 송출화면 숨김 상태 (구 응급 F2B 검정). 이후 TAKE·재생 시 자동 해제.
    bool    m_blackoutActive = false;
    // 자동 진행 일시정지 상태. m_currentProgramId/PageIdx 유지, 타이머 stop,
    // 영상 레이어 freeze. 재개 시 저장된 잔여 시간으로 타이머 재무장 + 재개.
    bool    m_playbackPaused = false;
    // 페이지 시작 절대 시각 (currentMSecsSinceEpoch). pause 시 elapsed 계산용.
    // 0 = 시작 시각 미기록 (자동 진행 미사용 페이지 또는 pause 대상 아님).
    qint64  m_pageStartMs    = 0;
    // pause 시 저장한 남은 밀리초. resume 시 이 값으로 timer 재시작.
    int     m_pauseRemainMs  = 0;
    // OFF 로 인해 자동 pause 처리했는지 — true 면 ON 시 자동 resume.
    // false 면(이미 사용자 ⏸ 상태였으면) ON 시 마스크만 해제.
    bool    m_autoPausedByOff = false;
    QTimer* m_editSaveTimer  = nullptr;   // 편집 자동저장 디바운스
    bool    m_suppressEditSave = false;   // 프로그램 로드 중 모델변경을 편집으로 오인 방지

    // Phase 5c — program 자동 진행 타이머 (displayTimeSec 만료).
    QTimer* m_programAdvanceTimer = nullptr;

    // LiveMirror — 우상단 "Live 송출" 미러 폴링 (qt/obs 자동 분기).
    QTimer* m_liveMirrorTimer = nullptr;
    bool    m_liveMirrorInFlight = false;   // obs 비동기 응답 대기 중 중복 방지
    // 재생 진행 바 — Live 대표 영상 위치/길이 폴링 (qt/obs 자동 분기).
    QTimer* m_playbackTimer    = nullptr;
    bool    m_playbackInFlight = false;
    // 재생 길이 모드 — 대표 영상 위치/길이로 자동 진행 타이머를 맞춘다.
    //   m_expectVideoLabel: 이번 페이지 대표 영상 라벨(이전 페이지 응답 배제용).
    //   m_videoEndLocked  : 종료 1초 전부터는 재무장 중단(반복 영상 되감김 대비).
    bool    m_waitVideoEnd     = false;
    bool    m_videoEndLocked   = false;
    QString m_expectVideoLabel;
    ILiveSink* currentLiveSink() const;     // engine + fallback 반영

    // BGM 재생기 — 프로그램 단위 배경음. playProgram 시 시작, 다른 프로그램
    //   전환/정지 시 stop.  편집(Preview) 중에는 재생하지 않음.
    std::unique_ptr<AudioPlayer> m_audio;
    void applyBgmFor(const Program& p);   // 파일/볼륨/loop 판단해 play 또는 stop
    void stopBgm();
};

} // namespace uwp
