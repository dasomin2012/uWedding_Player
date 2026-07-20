#pragma once

#include <QObject>
#include <QString>
#include <memory>

#include "Settings.h"

class QTimer;

namespace uwp {

class ControlWindow;
class LiveWindow;
class LivePlayerPool;
class SnapshotCache;
class SceneModel;
class TakeController;
class NovaStarController;
class ProgramRepository;
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

private slots:
    void onSelectOutputMonitorRequested();
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
    void onProgramDisplayTimeEditRequested(const QString& id);

private:
    QString resolveSettingsPath() const;
    QString resolveScenePath() const;
    QString dataDir() const;                         // <appDir>/data
    QString resolveProgramsPath() const;             // <appDir>/data/programs.json

    // Phase 5b — 편집 자동저장 (현재 편집 대상 program 으로 binding)
    void scheduleEditSave();                         // 씬 변경 → 디바운스 타이머 재시작
    void persistEditProgram();                       // 편집 대상에 layers+썸네일 저장
    void flushEditSave();                            // 대기중 저장을 즉시 반영

    // Phase 5c — program 재생/자동 진행
    void playProgram(const QString& id);             // 공통 재생 경로(수동/자동 공용)
    void onProgramAdvance();                         // displayTime 만료 → endAction 처리
    void stopProgramPlayback();                      // Stop: Live 클리어 + 타이머 정지
    QString currentNovaPresetId() const;            // O5: env > settings.default_preset_id
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

    // Phase 5b/5c — 현재 재생중(Play) program id ("" = 없음).
    QString m_currentProgramId;

    // Phase 5b — 현재 편집 대상 program id ("" = 스크래치, 자동저장 안 함).
    QString m_editProgramId;
    QTimer* m_editSaveTimer  = nullptr;   // 편집 자동저장 디바운스
    bool    m_suppressEditSave = false;   // 프로그램 로드 중 모델변경을 편집으로 오인 방지

    // Phase 5c — program 자동 진행 타이머 (displayTimeSec 만료).
    QTimer* m_programAdvanceTimer = nullptr;

    // LiveMirror — 우상단 "Live 송출" 미러 폴링 (qt/obs 자동 분기).
    QTimer* m_liveMirrorTimer = nullptr;
    bool    m_liveMirrorInFlight = false;   // obs 비동기 응답 대기 중 중복 방지
    ILiveSink* currentLiveSink() const;     // engine + fallback 반영
};

} // namespace uwp
