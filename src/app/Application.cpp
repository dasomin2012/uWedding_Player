#include "Application.h"

#include "windows/ControlWindow.h"
#include "windows/LiveWindow.h"
#include "player/LivePlayerPool.h"
#include "player/SnapshotCache.h"
#include "scene/SceneModel.h"
#include "scene/SceneSerializer.h"
#include "take/TakeController.h"
#include "novastar/NovaStarController.h"

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

// O5: 본 단계 임시 매핑(Phase 5 ProgramRepository 도입 전).
//     UWP_NOVASTAR_PRESET 환경변수 > settings.novastar.default_preset_id > "" (no-op).
QString Application::currentNovaPresetId() const {
    const QString env = qEnvironmentVariable("UWP_NOVASTAR_PRESET");
    if (!env.isEmpty()) return env;
    return m_settings.novaStar().defaultPresetId;
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
                    qCritical() << "OBS failed — falling back to qt:" << r;
                    installQtFallback(r);
                });
        sink = m_obsBackend.get();
        qInfo() << "engine=obs — launching managed OBS";
        m_obsProc->start();
    }
#endif

    // ----- NovaStar 동기 (O5) -----
    m_novaStar = std::make_unique<NovaStarController>(
        m_settings.novaStar(), this);

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

    // O5: take 완료 → NovaStar preset 호출.
    //  - OBS 정상 경로: ObsLiveBackend::transitionEnded 가 정확한 LED 동기 타이밍을 담당.
    //  - qt 경로 / OBS→qt 폴백 이후: TakeController::taken 에서 1회.
    //  m_qtFallbackActive 는 installQtFallback() 에서 true 로 세팅됨.
    connect(m_takeController.get(), &TakeController::taken, this,
            [this](int) {
                const bool isObs = (m_settings.engine().compare(
                    QLatin1String("obs"), Qt::CaseInsensitive) == 0);
                if (!isObs || m_qtFallbackActive)
                    m_novaStar->callPreset(currentNovaPresetId());
            });
#if defined(UWP_HAS_OBS)
    if (useObs && m_obsBackend) {
        connect(m_obsBackend.get(), &ObsLiveBackend::transitionEnded, this,
                [this]() { m_novaStar->callPreset(currentNovaPresetId()); });
    }
#endif

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

#if defined(UWP_HAS_OBS)
// O6-A: OBS 가 재기동 한계를 초과해 Failed 로 떨어지면 본식이 멈추지
// 않도록 qt(LiveWindow/libVLC) 백엔드로 자동 폴백한다.
void Application::installQtFallback(const QString& reason) {
    // 1) TakeController 가 OBS 백엔드를 더는 참조하지 않도록 즉시 단절
    if (m_takeController)
        m_takeController->setSink(m_liveWindow.get());

    // 2) 새 sink(qt)에 캔버스 재주입
    m_liveWindow->setCanvasSize(m_settings.canvasWidth(),
                                m_settings.canvasHeight());

    // 2-R1) 응급 폴백: 첫 take 는 Cut 강제(검정 시간 최소화).
    //       사용자가 설정한 모드는 다음 이벤트 루프 턴에 복귀 → 후속 take 정상.
    //       (OBS 가 막 죽은 직후라 dip-to-black 800ms 가 한 번 더 끼이는 것보다
    //        가장 빠른 복구가 본식 안전성에 유리.)
    TransitionEffect::Mode savedMode = TransitionEffect::Mode::Fade;
    if (m_takeController) {
        savedMode = m_takeController->mode();
        m_takeController->setMode(TransitionEffect::Mode::Cut);
    }

    // 3) OBS 경로에서 숨겨져 있던 LiveWindow 표시
    m_liveWindow->showOnMonitor(m_settings.outputMonitorIndex());

    // 4) 현재 Preview 를 즉시 다시 take (Live 검정 회피 — best-effort).
    //    이 시점에서 m_qtFallbackActive 는 아직 false → taken 핸들러는
    //    engine=obs && !fallback 으로 보고 NovaStar 콜을 스킵. take() 가
    //    !m_scene/!m_live 로 일찍 리턴해 taken 이 emit 안 될 수도 있다.
    if (m_takeController) m_takeController->take();

    // 4-O5) 위와 무관하게 폴백 응급 take 의 LED 동기는 여기서 1회 명시 호출.
    if (m_novaStar) m_novaStar->callPreset(currentNovaPresetId());

    // 4-O5-flag) 이후의 일반 Take(qt 백엔드 사용 중이지만 settings.engine
    //   값은 "obs" 인 상태)에서도 taken 핸들러가 NovaStar 콜을 발사하도록
    //   플래그 ON. 응급 take 의 이중 호출은 위 순서로 구조적으로 차단.
    m_qtFallbackActive = true;

    // 4-R1) 사용자가 설정했던 전환 모드 복귀 — 다음 이벤트 루프 턴.
    QMetaObject::invokeMethod(this, [this, savedMode]() {
        if (m_takeController) m_takeController->setMode(savedMode);
    }, Qt::QueuedConnection);

    // 5) OBS 리소스 정리는 다음 이벤트 루프 턴에. 지금은 m_obsProc 의
    //    failed 시그널 emit 컨텍스트 위라 즉시 reset 시 크래시.
    //    역순 소멸 규칙대로 backend 먼저, proc 나중.
    QMetaObject::invokeMethod(this, [this]() {
        m_obsBackend.reset();
        m_obsProc.reset();
    }, Qt::QueuedConnection);

    // 6) UI 안내
    m_controlWindow->setStatusText(
        tr("OBS 실패 — qt 백엔드로 폴백됨: %1").arg(reason));
    qInfo() << "Application: qt fallback installed — reason:" << reason;
}
#endif

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
