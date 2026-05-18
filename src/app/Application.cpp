#include "Application.h"

#include "windows/ControlWindow.h"
#include "windows/LiveWindow.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QInputDialog>
#include <QMessageBox>
#include <QScreen>
#include <QStringList>
#include <QDebug>

namespace uwp {

Application::Application(QObject* parent)
    : QObject(parent)
{}

Application::~Application() = default;

QString Application::resolveSettingsPath() const {
    // 실행파일과 같은 위치의 data/ 를 사용. 없으면 생성.
    const QString dataDir = QCoreApplication::applicationDirPath() + "/data";
    QDir().mkpath(dataDir);
    return dataDir + "/settings.json";
}

bool Application::initialize() {
    m_settingsPath = resolveSettingsPath();

    if (!m_settings.load(m_settingsPath)) {
        qInfo() << "Settings: file not found, creating defaults at" << m_settingsPath;
        m_settings.save(m_settingsPath);
    } else {
        qInfo() << "Settings: loaded from" << m_settingsPath;
    }

    // ----- 윈도우 생성 -----
    m_controlWindow = std::make_unique<ControlWindow>(&m_settings);
    m_liveWindow    = std::make_unique<LiveWindow>();
    m_liveWindow->setCanvasSize(m_settings.canvasWidth(), m_settings.canvasHeight());

    connect(m_controlWindow.get(), &ControlWindow::selectOutputMonitorRequested,
            this, &Application::onSelectOutputMonitorRequested);
    connect(m_controlWindow.get(), &ControlWindow::openSettingsRequested,
            this, &Application::onOpenSettingsRequested);

    // ----- 표시 -----
    m_controlWindow->show();
    m_liveWindow->showOnMonitor(m_settings.outputMonitorIndex());

    // ----- Phase 1: 테스트 영상 자동 재생 -----
    const QString videoPath = m_settings.testVideoPath();
    if (videoPath.isEmpty()) {
        qInfo() << "settings.test_video_path is empty — Live window stays black. "
                   "Set it in" << m_settingsPath;
    } else if (!QFileInfo::exists(videoPath)) {
        qWarning() << "test_video_path does not exist:" << videoPath;
    } else {
        m_liveWindow->playVideo(videoPath);
    }

    return true;
}

void Application::shutdown() {
    if (m_liveWindow) m_liveWindow->stopVideo();
}

void Application::onSelectOutputMonitorRequested() {
    const auto screens = QGuiApplication::screens();
    QStringList items;
    items.reserve(screens.size());
    for (int i = 0; i < screens.size(); ++i) {
        const QRect g = screens[i]->geometry();
        items << QString("%1: %2 (%3x%4 @%5,%6)")
                     .arg(i)
                     .arg(screens[i]->name())
                     .arg(g.width()).arg(g.height())
                     .arg(g.x()).arg(g.y());
    }

    const int currentIdx = m_settings.outputMonitorIndex();
    const int initial    = (currentIdx >= 0 && currentIdx < items.size()) ? currentIdx : 0;

    bool ok = false;
    const QString choice = QInputDialog::getItem(
        m_controlWindow.get(),
        tr("Select Output Monitor"),
        tr("Output monitor:"),
        items, initial, /*editable=*/false, &ok);

    if (!ok) return;

    const int newIdx = choice.section(':', 0, 0).toInt();
    m_settings.setOutputMonitorIndex(newIdx);
    m_settings.save(m_settingsPath);
    m_liveWindow->showOnMonitor(newIdx);

    // 영상이 재생 중이었다면 다시 재생 (HWND 가 새 위치로 이동했을 수 있음)
    const QString v = m_settings.testVideoPath();
    if (!v.isEmpty() && QFileInfo::exists(v)) {
        m_liveWindow->playVideo(v);
    }
}

void Application::onOpenSettingsRequested() {
    QMessageBox::information(
        m_controlWindow.get(),
        tr("Settings"),
        tr("Phase 1: edit data/settings.json manually.\n\nPath:\n%1").arg(m_settingsPath));
}

} // namespace uwp
