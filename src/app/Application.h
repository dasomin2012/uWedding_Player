#pragma once

#include <QObject>
#include <QString>
#include <memory>

#include "Settings.h"

namespace uwp {

class ControlWindow;
class LiveWindow;

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

private:
    QString resolveSettingsPath() const;

    Settings                       m_settings;
    QString                        m_settingsPath;
    std::unique_ptr<ControlWindow> m_controlWindow;
    std::unique_ptr<LiveWindow>    m_liveWindow;
};

} // namespace uwp
