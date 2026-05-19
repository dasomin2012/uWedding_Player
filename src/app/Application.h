#pragma once

#include <QObject>
#include <QString>
#include <memory>

#include "Settings.h"

namespace uwp {

class ControlWindow;
class LiveWindow;
class LivePlayerPool;
class SnapshotCache;
class SceneModel;
class TakeController;
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

private:
    QString resolveSettingsPath() const;
    QString resolveScenePath() const;
#if defined(UWP_HAS_OBS)
    void installQtFallback(const QString& reason);  // O6-A: OBS Failed → qt
#endif

    Settings                        m_settings;
    QString                         m_settingsPath;
    std::unique_ptr<LivePlayerPool> m_playerPool;     // 윈도우보다 오래 살아야 함
    std::unique_ptr<SnapshotCache>  m_snapshotCache;
    std::unique_ptr<SceneModel>     m_scene;
    std::unique_ptr<ControlWindow>  m_controlWindow;
    std::unique_ptr<LiveWindow>     m_liveWindow;
#if defined(UWP_HAS_OBS)
    // 선언 순서 주의: backend 를 proc 보다 먼저 선언 → proc 가 먼저 소멸
    // (proc 소멸 시 OBS 종료·ObsClient abort, 이때 backend 는 아직 생존).
    std::unique_ptr<ObsLiveBackend>    m_obsBackend;
    std::unique_ptr<ObsProcessManager> m_obsProc;
#endif
    std::unique_ptr<TakeController> m_takeController;
};

} // namespace uwp
