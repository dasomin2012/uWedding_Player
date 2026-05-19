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

    Settings                        m_settings;
    QString                         m_settingsPath;
    std::unique_ptr<LivePlayerPool> m_playerPool;     // 윈도우보다 오래 살아야 함
    std::unique_ptr<SnapshotCache>  m_snapshotCache;
    std::unique_ptr<SceneModel>     m_scene;
    std::unique_ptr<ControlWindow>  m_controlWindow;
    std::unique_ptr<LiveWindow>     m_liveWindow;
    std::unique_ptr<TakeController> m_takeController;
};

} // namespace uwp
