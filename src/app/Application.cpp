#include "Application.h"

#include "windows/ControlWindow.h"
#include "windows/LiveWindow.h"
#include "player/LivePlayerPool.h"
#include "player/SnapshotCache.h"
#include "scene/SceneModel.h"
#include "scene/SceneSerializer.h"
#include "take/TakeController.h"

#if defined(UWP_HAS_OBS)
#include "obs/ObsClient.h"
#include "obs/ObsProcessManager.h"
#include "obs/ObsLiveBackend.h"
#include <QJsonObject>
#include <QMetaEnum>
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QScreen>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
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

QString Application::resolveScenePath() const {
    QString p = m_settings.sceneScratch();
    if (QDir::isRelativePath(p)) {
        p = QDir(QCoreApplication::applicationDirPath()).filePath(p);
    }
    QDir().mkpath(QFileInfo(p).absolutePath());
    return p;
}

bool Application::initialize() {
    m_settingsPath = resolveSettingsPath();

    if (!m_settings.load(m_settingsPath)) {
        qInfo() << "Settings: file not found, creating defaults at" << m_settingsPath;
        m_settings.save(m_settingsPath);
    } else {
        qInfo() << "Settings: loaded from" << m_settingsPath;
    }

    // ----- 미디어 서브시스템 (윈도우보다 먼저, 더 오래 살아야 함) -----
    m_playerPool    = std::make_unique<LivePlayerPool>();
    m_snapshotCache = std::make_unique<SnapshotCache>(&m_settings);

    // ----- 씬 모델 (편집 단일 진실 소스) -----
    m_scene = std::make_unique<SceneModel>();
    m_scene->setCanvasSize(QSize(m_settings.canvasWidth(),
                                 m_settings.canvasHeight()));
    const QString scenePath = resolveScenePath();
    if (SceneSerializer::loadScene(*m_scene, scenePath)) {
        qInfo() << "Scene: loaded scratch scene from" << scenePath;
    }

    // ----- 윈도우 생성 -----
    m_controlWindow = std::make_unique<ControlWindow>(
        &m_settings, m_scene.get(), m_snapshotCache.get());
    m_liveWindow    = std::make_unique<LiveWindow>(m_playerPool.get());
    m_liveWindow->setCanvasSize(m_settings.canvasWidth(), m_settings.canvasHeight());

    connect(m_controlWindow.get(), &ControlWindow::selectOutputMonitorRequested,
            this, &Application::onSelectOutputMonitorRequested);
    connect(m_controlWindow.get(), &ControlWindow::openSettingsRequested,
            this, &Application::onOpenSettingsRequested);
    connect(m_controlWindow.get(), &ControlWindow::playTestVideoRequested,
            this, &Application::onPlayTestVideoRequested);
    connect(m_controlWindow.get(), &ControlWindow::saveSceneRequested,
            this, &Application::onSaveSceneRequested);
    connect(m_controlWindow.get(), &ControlWindow::loadSceneRequested,
            this, &Application::onLoadSceneRequested);

    // ----- 송출 백엔드 선택 (engine: qt | obs) -----
    ILiveSink* sink   = m_liveWindow.get();
    bool       useObs = false;
#if defined(UWP_HAS_OBS)
    useObs = (m_settings.engine().compare(QLatin1String("obs"),
                                          Qt::CaseInsensitive) == 0);
    if (useObs) {
        m_obsProc    = std::make_unique<ObsProcessManager>(m_settings.obs());
        m_obsBackend = std::make_unique<ObsLiveBackend>(
            m_obsProc.get(), m_settings.obs());
        m_obsBackend->setCanvasSize(m_settings.canvasWidth(),
                                    m_settings.canvasHeight());
        connect(m_obsProc.get(), &ObsProcessManager::stateChanged, this,
                [](ObsProcessManager::State s) {
                    qInfo() << "ObsProcessManager state ="
                            << QMetaEnum::fromType<ObsProcessManager::State>()
                                   .valueToKey(static_cast<int>(s));
                });
        connect(m_obsProc.get(), &ObsProcessManager::failed, this,
                [this](const QString& r) {
                    qCritical() << "OBS failed:" << r;
                    m_controlWindow->setStatusText(
                        tr("OBS failed: %1").arg(r));
                });
        sink = m_obsBackend.get();
        qInfo() << "engine=obs — launching managed OBS";
        m_obsProc->start();
    }
#endif

    // ----- Take (Preview SceneModel -> Live) -----
    m_takeController = std::make_unique<TakeController>(
        m_scene.get(), sink, &m_settings);
    connect(m_controlWindow.get(), &ControlWindow::takeRequested,
            m_takeController.get(), &TakeController::take);
    connect(m_controlWindow.get(), &ControlWindow::takeModeChanged,
            this, [this](const QString& mode) {
                const auto m = (mode.toLower() == "cut")
                    ? TransitionEffect::Mode::Cut
                    : TransitionEffect::Mode::Fade;
                m_takeController->setMode(m);
                m_settings.setTakeDefaultMode(mode.toLower());
                m_settings.save(m_settingsPath);
            });
    connect(m_takeController.get(), &TakeController::taken,
            this, [this](int n) {
                m_controlWindow->setStatusText(tr("Take: %1 layer(s) → Live").arg(n));
            });

    // 스냅샷 실패는 상태바로 안내 (성공은 PreviewCanvas/MediaList 가 직접 수신)
    connect(m_snapshotCache.get(), &SnapshotCache::snapshotFailed,
            this, [this](const QString& media, const QString& reason) {
                qWarning() << "Snapshot failed:" << media << "-" << reason;
                m_controlWindow->setStatusText(
                    tr("Snapshot failed: %1 (%2)").arg(media, reason));
            });

    // ----- 표시 -----
    m_controlWindow->show();
    if (!useObs) {
        m_liveWindow->showOnMonitor(m_settings.outputMonitorIndex());
    } else {
        qInfo() << "engine=obs — Live output via OBS projector "
                   "(LiveWindow hidden)";
    }

    // ----- Phase 1: 테스트 영상 자동 재생 (qt 백엔드 전용) -----
    if (!useObs) {
        const QString videoPath = m_settings.testVideoPath();
        if (videoPath.isEmpty()) {
            qInfo() << "settings.test_video_path is empty — Live window "
                       "stays black. Set it in" << m_settingsPath;
        } else if (!QFileInfo::exists(videoPath)) {
            qWarning() << "test_video_path does not exist:" << videoPath;
        } else {
            m_liveWindow->playVideo(videoPath);  // Live: 실제 재생 (편집과 독립)
        }
    }

    // 검증/자동화 훅: UWP_AUTOTAKE=1 이면 시작 후 자동으로 Take 1회.
    if (qEnvironmentVariableIntValue("UWP_AUTOTAKE") > 0) {
        QTimer::singleShot(1500, m_takeController.get(), &TakeController::take);
        qInfo() << "UWP_AUTOTAKE enabled — auto Take in 1500ms";
    }

#if defined(UWP_HAS_OBS)
    // O2 PoC: UWP_OBS_PING=1 이면 (수동 기동한) OBS 에 obs-websocket 으로
    // 접속해 GetVersion 1회 호출·로깅. O3/O4 에서 정식 경로로 대체된다.
    if (qEnvironmentVariableIntValue("UWP_OBS_PING") > 0) {
        const auto cfg = m_settings.obs();
        auto* obs = new ObsClient(this);   // QObject 부모 소유 → 앱 수명 동안 생존
        connect(obs, &ObsClient::ready, this, [obs]() {
            qInfo() << "ObsClient: identified — sending GetVersion";
            obs->request(QStringLiteral("GetVersion"), {},
                [](bool ok, const QJsonObject& d, const QString& c) {
                    if (ok)
                        qInfo() << "OBS GetVersion OK — obsVersion="
                                << d.value("obsVersion").toString()
                                << "obsWebSocketVersion="
                                << d.value("obsWebSocketVersion").toString();
                    else
                        qWarning() << "OBS GetVersion failed:" << c;
                });
        });
        connect(obs, &ObsClient::socketError, this, [](const QString& m) {
            qWarning() << "ObsClient socket error:" << m;
        });
        connect(obs, &ObsClient::closed, this, []() {
            qInfo() << "ObsClient: connection closed";
        });
        qInfo() << "UWP_OBS_PING enabled — connecting to" << cfg.wsUrl;
        obs->connectToObs(cfg.wsUrl, cfg.wsPassword);
    }
    // 관리형 OBS 수명/송출은 engine=obs 경로(위)에서 처리. UWP_OBS_START
    // 단독 훅은 O4 에서 정식 경로로 대체되어 제거됨.
#endif

    return true;
}

void Application::shutdown() {
    if (m_liveWindow) m_liveWindow->stopVideo();
    if (m_scene) {
        SceneSerializer::saveScene(*m_scene, resolveScenePath());
    }
    if (!m_settingsPath.isEmpty()) {
        m_settings.save(m_settingsPath);   // media_dir 등 보존
    }
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

void Application::onPlayTestVideoRequested() {
    const QString start = m_settings.testVideoPath().isEmpty()
                              ? QCoreApplication::applicationDirPath()
                              : m_settings.testVideoPath();

    const QString path = QFileDialog::getOpenFileName(
        m_controlWindow.get(),
        tr("Play Test Video"),
        start,
        tr("Video files (*.mp4 *.mov *.avi *.mkv *.wmv);;All files (*.*)"));

    if (path.isEmpty()) return;

    if (m_liveWindow->playVideo(path)) {
        m_settings.setTestVideoPath(path);
        m_settings.save(m_settingsPath);
        m_controlWindow->setStatusText(tr("Live playing: %1").arg(path));
    } else {
        QMessageBox::warning(
            m_controlWindow.get(),
            tr("Playback Failed"),
            tr("Could not play:\n%1\n\n"
               "If libVLC SDK was missing at build time, video is disabled.").arg(path));
    }
}

void Application::onSaveSceneRequested() {
    const QString p = resolveScenePath();
    if (SceneSerializer::saveScene(*m_scene, p))
        m_controlWindow->setStatusText(tr("Scene saved: %1").arg(p));
    else
        m_controlWindow->setStatusText(tr("Scene save failed"));
}

void Application::onLoadSceneRequested() {
    const QString p = resolveScenePath();
    if (SceneSerializer::loadScene(*m_scene, p))
        m_controlWindow->setStatusText(tr("Scene loaded: %1").arg(p));
    else
        m_controlWindow->setStatusText(tr("No scene file at %1").arg(p));
}

} // namespace uwp
