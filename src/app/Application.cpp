#include "Application.h"

#include "windows/ControlWindow.h"
#include "windows/LiveWindow.h"
#include "live/ILiveSink.h"
#include "player/LivePlayerPool.h"
#include "player/SnapshotCache.h"
#include "scene/SceneModel.h"
#include "scene/SceneSerializer.h"
#include "take/TakeController.h"
#include "novastar/NovaStarController.h"
#include "program/ProgramRepository.h"
#include "program/ProgramListWidget.h"
#include "editor/PreviewCanvas.h"

#if defined(UWP_HAS_OBS)
#include "obs/ObsClient.h"
#include "obs/ObsProcessManager.h"
#include "obs/ObsLiveBackend.h"
#include <QJsonObject>
#include <QMetaEnum>
#endif

#include <QButtonGroup>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QGuiApplication>
#include <QFileDialog>
#include <QImage>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QPointer>
#include <QRadioButton>
#include <QScreen>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QFile>
#include <QPixmap>
#include <QVBoxLayout>
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

QString Application::dataDir() const {
    const QString d = QCoreApplication::applicationDirPath() + "/data";
    QDir().mkpath(d);
    return d;
}

QString Application::resolveProgramsPath() const {
    return dataDir() + "/programs.json";       // D3: settings.json 과 분리
}

// LiveMirror: 현재 활성 sink 반환. 폴백 상태(m_qtFallbackActive)까지 반영.
ILiveSink* Application::currentLiveSink() const {
#if defined(UWP_HAS_OBS)
    if (m_obsBackend && !m_qtFallbackActive)
        return static_cast<ILiveSink*>(m_obsBackend.get());
#endif
    return static_cast<ILiveSink*>(m_liveWindow.get());
}

// Phase 5c: program 인식 룩업.
//   UWP_NOVASTAR_PRESET > 현재 재생 program 의 novastarPresetId
//   > settings.novastar.default_preset_id > "" (no-op).
QString Application::currentNovaPresetId() const {
    const QString env = qEnvironmentVariable("UWP_NOVASTAR_PRESET");
    if (!env.isEmpty()) return env;
    if (m_programs && !m_currentProgramId.isEmpty()) {
        if (const Program* p = m_programs->find(m_currentProgramId))
            if (!p->novastarPresetId.isEmpty())
                return p->novastarPresetId;
    }
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

    // 리허설(미리보기 재생) 창 — Control 모니터에 뜨는 non-fullscreen LiveWindow.
    // 캔버스 비율 유지하며 폭 800px 상한, 높이 600px 상한.
    m_rehearsalWindow = std::make_unique<LiveWindow>(m_playerPool.get());
    m_rehearsalWindow->setWindowTitle(tr("미리보기 재생"));
    m_rehearsalWindow->setCanvasSize(m_settings.canvasWidth(),
                                     m_settings.canvasHeight());
    {
        const int cw = m_settings.canvasWidth();
        const int ch = m_settings.canvasHeight();
        double asp = (ch > 0) ? double(cw) / double(ch) : 16.0/9.0;
        int w = 800;
        int h = qRound(w / asp);
        if (h > 600) { h = 600; w = qRound(h * asp); }
        m_rehearsalWindow->resize(w, h);
    }
    m_rehearsalWindow->installEventFilter(this);
    connect(m_controlWindow.get(), &ControlWindow::previewPlayingChanged,
            this, &Application::onPreviewPlayingChanged);
    connect(m_controlWindow.get(), &ControlWindow::previewCompleted,
            this, &Application::onPreviewCompleted);

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

    // ----- Program 리스트 (Phase 5a — 표시만; 추가/삭제/재생은 5b) -----
    m_programs = std::make_unique<ProgramRepository>(this);
    if (auto* pl = m_controlWindow->programList()) {
        auto refresh = [this, pl]() {
            pl->setPrograms(m_programs->programs(), dataDir());
        };
        connect(m_programs.get(), &ProgramRepository::programsReloaded, this, refresh);
        connect(m_programs.get(), &ProgramRepository::programAdded,   this, refresh);
        connect(m_programs.get(), &ProgramRepository::programRemoved, this, refresh);
        // 편집 자동저장은 빈번하므로 update 는 전체 재구성 대신 단일 항목만 갱신.
        connect(m_programs.get(), &ProgramRepository::programUpdated, this,
                [this, pl](const QString& id) {
                    const Program* p = m_programs->find(id);
                    if (!p) return;
                    const QString thumb = p->thumbnailRelPath.isEmpty()
                        ? QString() : dataDir() + "/" + p->thumbnailRelPath;
                    pl->updateItem(id, *p, thumb);
                });

        // Phase 5b — 리스트 동작 와이어링
        connect(pl, &ProgramListWidget::addRequested,
                this, &Application::onProgramAddRequested);
        connect(pl, &ProgramListWidget::programSelected,
                this, &Application::onProgramSelected);
        connect(pl, &ProgramListWidget::playRequested,
                this, &Application::onProgramPlayRequested);
        connect(pl, &ProgramListWidget::renameRequested,
                this, &Application::onProgramRenameRequested);
        connect(pl, &ProgramListWidget::deleteRequested,
                this, &Application::onProgramDeleteRequested);
        connect(pl, &ProgramListWidget::displayTimeEditRequested,
                this, &Application::onProgramDisplayTimeEditRequested);
        connect(pl, &ProgramListWidget::endActionEditRequested,
                this, &Application::onProgramEndActionEditRequested);
    }
    // 부재 시 빈 리스트로 시작(에러 아님). 손상 시 .bak 백업 후 빈 리스트.
    m_programs->load(resolveProgramsPath());

    // Phase 5b — 편집 자동저장: 씬 변경 → 디바운스 → 현재 편집 program 에 반영.
    m_editSaveTimer = new QTimer(this);
    m_editSaveTimer->setSingleShot(true);
    m_editSaveTimer->setInterval(500);
    connect(m_editSaveTimer, &QTimer::timeout, this, &Application::persistEditProgram);
    connect(m_scene.get(), &SceneModel::layerAdded,    this, [this](const QString&){ scheduleEditSave(); });
    connect(m_scene.get(), &SceneModel::layerRemoved,  this, [this](const QString&){ scheduleEditSave(); });
    connect(m_scene.get(), &SceneModel::layerChanged,  this, [this](const QString&){ scheduleEditSave(); });
    connect(m_scene.get(), &SceneModel::zOrderChanged, this, [this](){ scheduleEditSave(); });

    // Phase 5c — program 자동 진행 타이머 (displayTimeSec 만료 → endAction).
    m_programAdvanceTimer = new QTimer(this);
    m_programAdvanceTimer->setSingleShot(true);
    connect(m_programAdvanceTimer, &QTimer::timeout,
            this, &Application::onProgramAdvance);

    // LiveMirror — ControlWindow 우상단 "Live 송출" 미러 폴러.
    //   qt 백엔드: LiveWindow(Win32 PrintWindow) — 동기, 매 400ms 캡처.
    //   obs 백엔드: GetSourceScreenshot — 비동기, 응답 도착 후 다음 요청.
    // 요청/응답 상관: 백엔드가 소멸해도 콜백에서 QPointer 로 ControlWindow
    // 유효성 확인 후 반영(수명 안전).
    m_liveMirrorTimer = new QTimer(this);
    m_liveMirrorTimer->setInterval(400);
    QPointer<ControlWindow> ctrl = m_controlWindow.get();
    connect(m_liveMirrorTimer, &QTimer::timeout, this, [this, ctrl]() {
        if (m_liveMirrorInFlight) return;
        ILiveSink* sink = currentLiveSink();
        if (!sink || !ctrl) return;
        m_liveMirrorInFlight = true;
        sink->requestMirrorSnapshot(320, [this, ctrl](const QImage& img) {
            m_liveMirrorInFlight = false;
            if (ctrl) ctrl->setLiveMirrorImage(img);
        });
    });
    m_liveMirrorTimer->start();

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
    // 수동 TAKE →
    //   1) 진행중 자동 진행 중단(운용자 수동 제어 우선, §R5/4.3)
    //   2) UI-C: 프로그램이 Preview 로드된 상태였다면 그 program 을 "재생중" 으로
    //      확정 + 자동 진행 timer 재무장. (더블클릭이 더는 Play 를 유발하지 않으므로
    //      TAKE 가 Play 트리거 역할을 겸함.)
    connect(m_controlWindow.get(), &ControlWindow::takeRequested, this, [this]() {
        if (m_programAdvanceTimer) m_programAdvanceTimer->stop();
        if (m_editProgramId.isEmpty() || !m_programs) return;
        const Program* p = m_programs->find(m_editProgramId);
        if (!p) return;
        m_currentProgramId = m_editProgramId;
        if (auto* pl = m_controlWindow->programList())
            pl->setActiveProgram(m_currentProgramId);
        // UI-D Phase A: Program 단위 displayTime 은 페이지 0 값으로 위임.
        //   Phase C 에서 페이지 순회 지원 시 이 로직 재작성.
        if (!p->pages.isEmpty()
            && p->pages.first().displayTimeSec > 0
            && m_programAdvanceTimer)
            m_programAdvanceTimer->start(p->pages.first().displayTimeSec * 1000);
    });
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
    // 검증/자동화 훅: UWP_AUTOPLAY=1 이면 시작 후 첫 program 자동 Play
    // (Phase 5c 자동 진행·NovaStar 경로 검증용).
    if (qEnvironmentVariableIntValue("UWP_AUTOPLAY") > 0) {
        QTimer::singleShot(1500, this, [this]() {
            if (m_programs && m_programs->count() > 0)
                playProgram(m_programs->programs().first().id);
        });
        qInfo() << "UWP_AUTOPLAY enabled — auto Play first program in 1500ms";
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
    flushEditSave();   // 종료 전 대기중 편집 저장 확정
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

#if defined(UWP_HAS_OBS)
    // engine=obs: Live 출력은 LiveWindow 가 아니라 OBS 풀스크린 프로젝터다.
    // → settings.obs.projector_monitor 를 갱신하고 프로젝터를 새 모니터로 재오픈.
    //   (qt 폴백 중이면 아래 qt 경로로 처리)
    const bool obsActive =
        (m_settings.engine().compare(QLatin1String("obs"),
                                     Qt::CaseInsensitive) == 0)
        && m_obsProc && !m_qtFallbackActive;
    if (obsActive) {
        m_settings.setObsProjectorMonitor(newIdx);
        m_settings.save(m_settingsPath);
        m_obsProc->setProjectorMonitor(newIdx);
        m_controlWindow->setStatusText(
            tr("OBS projector → monitor %1").arg(newIdx));
        return;
    }
#endif

    m_settings.setOutputMonitorIndex(newIdx);
    m_settings.save(m_settingsPath);
    m_liveWindow->showOnMonitor(newIdx);

    // 모니터 이동 시 네이티브 윈도우가 재생성될 수 있으므로(플래그/스크린 변경)
    // 현재 씬을 새 모니터에 다시 commit → 재생 내용 유지 + HWND 재바인딩.
    if (m_takeController) m_takeController->take();
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

// ---- Phase 5b — Program 동작 -----------------------------------
// [+ Add] = 빈 Program 생성 + 편집 대상으로 바인딩. 이후 Preview 편집은
// 디바운스되어 이 program 에 자동 저장(layers + 썸네일).
void Application::onProgramAddRequested() {
    flushEditSave();   // 직전 편집 대상 저장 확정

    Program p;
    p.id        = m_programs->makeUniqueId();
    p.name      = tr("Program %1").arg(m_programs->count() + 1);
    p.endAction = EndAction::Hold;            // 자동진행 기본=정지(안전, D5)
    // 레이어 없음(빈 프로그램). 편집이 채운다.

    // Preview 를 비워 편집 시작점으로. (로드성 변경이므로 자동저장 억제)
    m_suppressEditSave = true;
    m_scene->clear();
    m_suppressEditSave = false;

    // 빈 캔버스 썸네일 1회 렌더
    const QString thumbsRel = QStringLiteral("programs/thumbs");
    QDir().mkpath(dataDir() + "/" + thumbsRel);
    if (auto* pc = m_controlWindow->previewCanvas()) {
        const QSize cs = m_scene->canvasSize();
        const int tw = 320;
        const int th = (cs.width() > 0)
            ? qMax(1, qRound(320.0 * cs.height() / cs.width())) : 180;
        const QPixmap pm  = pc->renderThumbnail(QSize(tw, th));
        const QString rel = thumbsRel + "/" + p.id + ".png";
        if (!pm.isNull() && pm.save(dataDir() + "/" + rel, "PNG"))
            p.thumbnailRelPath = rel;
    }

    m_programs->add(p);                       // → programAdded → 리스트 갱신
    m_programs->save(resolveProgramsPath());

    m_editProgramId = p.id;                    // 이후 편집은 이 program 에 저장
    if (auto* pl = m_controlWindow->programList()) pl->selectProgram(p.id);
    // Phase A: 현재는 첫 페이지만 편집 (Phase B/C 에서 페이지 전환 도입).
    m_controlWindow->setPreviewDisplayTime(p.pages.first().displayTimeSec);
    m_controlWindow->setStatusText(
        tr("Added (editing): %1 — 미디어를 배치하면 자동 저장됩니다").arg(p.name));
}

void Application::onProgramSelected(const QString& id) {
    flushEditSave();                           // 이전 편집 대상 저장 확정
    const Program* p = m_programs->find(id);
    if (!p) return;
    m_suppressEditSave = true;
    // Phase A: 첫 페이지 layers 를 SceneModel 로 (페이지 전환은 Phase B).
    m_scene->replaceAll(p->pages.first().layers);
    m_suppressEditSave = false;
    m_editProgramId = id;                      // 편집 대상 전환 → 이후 편집 자동저장
    m_controlWindow->setPreviewDisplayTime(p->pages.first().displayTimeSec);
    m_controlWindow->setStatusText(tr("Loaded: %1").arg(p->name));
}

void Application::onProgramPlayRequested(const QString& id) {
    playProgram(id);   // 수동 Play → 공통 경로(자동 진행 타이머 포함)
}

// ---- Phase 5c — 재생 / 자동 진행 -------------------------------
// 수동 Play 와 자동 진행이 공유하는 단일 경로.
void Application::playProgram(const QString& id) {
    flushEditSave();
    const Program* p = m_programs->find(id);
    if (!p) return;

    m_suppressEditSave = true;
    // Phase A: 첫 페이지 layers 만 재생. 페이지 순회는 Phase C.
    m_scene->replaceAll(p->pages.first().layers);
    m_suppressEditSave = false;
    m_editProgramId    = id;
    m_currentProgramId = id;            // take 전에 설정 → currentNovaPresetId 정확

    if (m_takeController) m_takeController->take();   // Live 송출 (+ taken→NovaStar)

    if (auto* pl = m_controlWindow->programList()) {
        pl->setActiveProgram(id);       // 재생중 강조
        pl->selectProgram(id);
    }
    const int firstPageTime = p->pages.first().displayTimeSec;
    m_controlWindow->setPreviewDisplayTime(firstPageTime);

    // 자동 진행: 첫 페이지 displayTimeSec>0 이면 타이머 시작.
    if (m_programAdvanceTimer) {
        m_programAdvanceTimer->stop();
        if (firstPageTime > 0) {
            m_programAdvanceTimer->start(firstPageTime * 1000);
            qInfo() << "Program advance armed:" << firstPageTime << "s,"
                    << "action=" << endActionToString(p->endAction)
                    << "program=" << p->name;
        }
    }
    m_controlWindow->setStatusText(tr("Playing: %1").arg(p->name));
}

void Application::onProgramAdvance() {
    if (m_currentProgramId.isEmpty()) return;
    const Program* cur = m_programs->find(m_currentProgramId);
    if (!cur) return;
    const EndAction act   = cur->endAction;
    const QString   curId = m_currentProgramId;

    switch (act) {
    case EndAction::Next: {
        const QString next = m_programs->nextIdAfter(curId, EndAction::Next);
        if (next.isEmpty()) {           // 마지막 → stop (R6: 무한루프 방지)
            qInfo() << "Program advance: end of list → stop";
            stopProgramPlayback();
        } else {
            playProgram(next);
        }
        break;
    }
    case EndAction::Loop:
        playProgram(curId);             // 자기 자신 재생(타이머 재무장)
        break;
    case EndAction::Stop:
        stopProgramPlayback();
        break;
    case EndAction::First: {
        // 마지막 → 첫 순환 (Next 의 wrap-around 형태). 목록 비어있으면 stop.
        if (m_programs->count() > 0)
            playProgram(m_programs->programs().first().id);
        else
            stopProgramPlayback();
        break;
    }
    case EndAction::Hold:
    default:
        break;                          // Live 유지, 타이머 만료로 정지
    }
}

void Application::stopProgramPlayback() {
    if (m_programAdvanceTimer) m_programAdvanceTimer->stop();
    if (m_takeController) m_takeController->clearLive();   // Live 비움(검정), 편집 무영향
    m_currentProgramId.clear();
    if (auto* pl = m_controlWindow->programList())
        pl->setActiveProgram(QString());
    m_controlWindow->setPreviewDisplayTime(-1);            // UI-F
    m_controlWindow->setStatusText(tr("Program stopped"));
}

// ---- Phase 5b — 편집 자동저장 (현재 편집 대상 program) ----------
void Application::scheduleEditSave() {
    if (m_suppressEditSave || m_editProgramId.isEmpty()) return;
    if (m_editSaveTimer) m_editSaveTimer->start();   // 디바운스 재시작
}

void Application::persistEditProgram() {
    if (m_editProgramId.isEmpty()) return;
    const Program* cur = m_programs->find(m_editProgramId);
    if (!cur) { m_editProgramId.clear(); return; }
    Program up = *cur;
    // Phase A: 편집 대상 = 첫 페이지. Phase B 부터 현재 페이지 인덱스 사용.
    up.pages.first().layers = m_scene->layers();

    if (auto* pc = m_controlWindow->previewCanvas()) {
        const QSize cs = m_scene->canvasSize();
        const int tw = 320;
        const int th = (cs.width() > 0)
            ? qMax(1, qRound(320.0 * cs.height() / cs.width())) : 180;
        const QPixmap pm = pc->renderThumbnail(QSize(tw, th));
        QString rel = up.thumbnailRelPath;
        if (rel.isEmpty()) rel = QStringLiteral("programs/thumbs/") + up.id + ".png";
        QDir().mkpath(QFileInfo(dataDir() + "/" + rel).absolutePath());
        if (!pm.isNull() && pm.save(dataDir() + "/" + rel, "PNG"))
            up.thumbnailRelPath = rel;
    }

    m_programs->update(up);                    // → programUpdated → updateItem
    m_programs->save(resolveProgramsPath());
}

void Application::flushEditSave() {
    if (m_editSaveTimer && m_editSaveTimer->isActive()) {
        m_editSaveTimer->stop();
        persistEditProgram();
    }
}

void Application::onProgramRenameRequested(const QString& id,
                                           const QString& newName) {
    const Program* p = m_programs->find(id);
    if (!p) return;
    Program up = *p;
    up.name = newName;
    m_programs->update(up);                   // → programUpdated → 갱신
    m_programs->save(resolveProgramsPath());
}

// 프로그램 카드 우클릭 → "종료 동작 설정..." — displayTime 만료 후 무엇을 할지.
//   Loop:  자기 자신 재생(반복).
//   Stop:  Live 검정 + 재생 종료.
//   Hold:  마지막 프레임 유지, 타이머 정지.
//   Next:  목록 순서 상 다음 프로그램 자동 재생 (마지막이면 정지).
//   First: 첫 번째 프로그램으로 (Next 의 마지막→첫 순환 형태).
//
// QInputDialog::getItem 은 콤보 드롭다운을 사용해 확장 모니터로 튀어나가는
// 사례가 있어, 라디오 버튼 방식 커스텀 QDialog 로 대체 — 모든 선택지가 항상
// 노출되어 추가 팝업이 없다.
void Application::onProgramEndActionEditRequested(const QString& id) {
    const Program* p = m_programs->find(id);
    if (!p) return;

    struct Item { QString label; EndAction ea; };
    const QVector<Item> items = {
        { tr("반복 재생 (같은 프로그램 반복)"),  EndAction::Loop  },
        { tr("정지 (Live 검정)"),               EndAction::Stop  },
        { tr("마지막 화면 유지"),                EndAction::Hold  },
        { tr("다음 프로그램으로"),               EndAction::Next  },
        { tr("첫 프로그램으로"),                 EndAction::First },
    };

    QDialog dlg(m_controlWindow.get());
    dlg.setWindowTitle(tr("프로그램 종료 동작"));
    auto* v = new QVBoxLayout(&dlg);
    v->addWidget(new QLabel(tr("표시 시간이 지나면:")));
    QButtonGroup group(&dlg);
    for (int i = 0; i < items.size(); ++i) {
        auto* rb = new QRadioButton(items[i].label, &dlg);
        rb->setChecked(items[i].ea == p->endAction);
        group.addButton(rb, i);
        v->addWidget(rb);
    }
    auto* bb = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    connect(bb, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    v->addWidget(bb);

    if (dlg.exec() != QDialog::Accepted) return;
    const int selected = group.checkedId();
    if (selected < 0) return;
    const EndAction ea = items[selected].ea;
    if (ea == p->endAction) return;

    Program up = *p;
    up.endAction = ea;
    m_programs->update(up);                    // → programUpdated → 카드 갱신
    m_programs->save(resolveProgramsPath());
    // 재생 중 프로그램의 종료 동작이 바뀌어도 타이머는 그대로 — 만료 시
    // 새 endAction 이 자연스럽게 조회되어 적용된다(별도 재무장 불필요).
}

// 프로그램 카드 우클릭 → "표시 시간 설정..." — 현재값을 초기값으로 다이얼로그.
// Phase A: 첫 페이지의 displayTimeSec 편집 (사실상 "프로그램 시간"과 동등).
// Phase C 부터는 페이지별 편집 UI 별도 도입, 이 다이얼로그는 페이지 UI 없는
// 상태의 편의 진입점으로 유지.
void Application::onProgramDisplayTimeEditRequested(const QString& id) {
    const Program* p = m_programs->find(id);
    if (!p) return;
    const int curSec = p->pages.first().displayTimeSec;
    bool ok = false;
    const int sec = QInputDialog::getInt(
        m_controlWindow.get(),
        tr("프로그램 표시 시간"),
        tr("자동 진행 초 (0 = 수동 · 최대 86400):"),
        curSec, 0, 86400, 1, &ok);
    if (!ok || sec == curSec) return;
    Program up = *p;
    up.pages.first().displayTimeSec = sec;
    m_programs->update(up);                   // → programUpdated → 카드 갱신
    m_programs->save(resolveProgramsPath());
    // 편집 대상이면 Preview 툴바 라벨/▶ 활성 즉시 갱신.
    if (m_editProgramId == id)
        m_controlWindow->setPreviewDisplayTime(sec);
    // 재생중이면 새 시간으로 자동진행 타이머 재무장 (기존 남은 시간 무시하고 리셋).
    if (m_currentProgramId == id && m_programAdvanceTimer) {
        m_programAdvanceTimer->stop();
        if (sec > 0) m_programAdvanceTimer->start(sec * 1000);
    }
}

void Application::onProgramDeleteRequested(const QString& id) {
    const Program* p = m_programs->find(id);
    if (!p) return;
    const QString name  = p->name;            // remove 전에 캡처(포인터 무효화 방지)
    const QString thumb = p->thumbnailRelPath;

    const auto reply = QMessageBox::question(
        m_controlWindow.get(), tr("Delete Program"),
        tr("Delete program \"%1\"?").arg(name));
    if (reply != QMessageBox::Yes) return;

    if (!thumb.isEmpty()) QFile::remove(dataDir() + "/" + thumb);
    if (m_editProgramId == id) {               // 편집 대상이 삭제됨 → 자동저장 중단
        if (m_editSaveTimer) m_editSaveTimer->stop();
        m_editProgramId.clear();
        m_controlWindow->setPreviewDisplayTime(-1);   // UI-F
    }
    if (m_currentProgramId == id) {
        m_currentProgramId.clear();
        if (auto* pl = m_controlWindow->programList())
            pl->setActiveProgram(QString());
    }
    m_programs->remove(id);                    // → programRemoved → 갱신
    m_programs->save(resolveProgramsPath());
    m_controlWindow->setStatusText(tr("Deleted: %1").arg(name));
}

// ▶ 클릭(true) / ⏸·리셋(false) 에 반응해 리허설 창 제어.
//   playing=true  : 현재 SceneModel 스냅샷을 리허설 창에 apply + 창 표시
//                   + 리허설 세션 트래킹용 m_previewProgramId 시작값 설정
//   playing=false : 창 숨김 + 씬 비움(플레이어 반환) + 세션 트래킹 클리어
// 자연 만료(auto-complete)는 previewCompleted 로 별도 처리 — endAction 체인.
void Application::onPreviewPlayingChanged(bool playing) {
    if (!m_rehearsalWindow) return;
    if (playing) {
        // 편집 중 상태를 그대로 스냅샷 — Take 와 달리 Live 는 무영향.
        m_rehearsalWindow->applyScene(m_scene->layers());
        m_rehearsalWindow->show();
        m_rehearsalWindow->raise();
        m_rehearsalWindow->activateWindow();
        // 리허설 세션 시작 — 현재 편집 대상을 초기 preview 프로그램으로.
        // 스크래치(m_editProgramId 빔)이면 세션은 열리되 체인 대상 없음.
        m_previewProgramId = m_editProgramId;
    } else {
        m_rehearsalWindow->applyScene({});      // 플레이어 반환
        m_rehearsalWindow->hide();
        m_previewProgramId.clear();
    }
}

// 카운트다운 자연 만료 시점 — 현재 preview 프로그램의 endAction 조회하여 체인.
void Application::onPreviewCompleted() {
    if (!m_rehearsalWindow || !m_controlWindow) return;

    // 스크래치 씬 또는 프로그램이 사라진 경우 → 세션 종료.
    auto finishSession = [this]{
        m_rehearsalWindow->applyScene({});
        m_rehearsalWindow->hide();
        m_previewProgramId.clear();
        m_controlWindow->resetPreviewSim();
    };
    if (m_previewProgramId.isEmpty()) { finishSession(); return; }
    const Program* p = m_programs ? m_programs->find(m_previewProgramId) : nullptr;
    if (!p) { finishSession(); return; }

    switch (p->endAction) {
    case EndAction::Loop:
        // 같은 프로그램 재적용 — 동영상이 처음부터 다시 재생됨.
        //  Phase A: 첫 페이지 기준. Phase C 에서 페이지 순회 지원 시 재구성.
        m_rehearsalWindow->applyScene(p->pages.first().layers);
        m_controlWindow->restartPreviewCountdown(
            p->pages.first().displayTimeSec);
        break;
    case EndAction::Next: {
        const QString nextId = m_programs->nextIdAfter(m_previewProgramId,
                                                        EndAction::Next);
        if (nextId.isEmpty()) { finishSession(); break; }   // 마지막 → stop
        const Program* np = m_programs->find(nextId);
        if (!np) { finishSession(); break; }
        m_previewProgramId = nextId;
        m_rehearsalWindow->applyScene(np->pages.first().layers);
        m_controlWindow->restartPreviewCountdown(
            np->pages.first().displayTimeSec);
        break;
    }
    case EndAction::First: {
        if (m_programs->count() == 0) { finishSession(); break; }
        const Program& fp = m_programs->programs().first();
        m_previewProgramId = fp.id;
        m_rehearsalWindow->applyScene(fp.pages.first().layers);
        m_controlWindow->restartPreviewCountdown(
            fp.pages.first().displayTimeSec);
        break;
    }
    case EndAction::Stop:
        finishSession();
        break;
    case EndAction::Hold:
    default:
        // 창 유지, 마지막 프레임 그대로. 카운터는 "MM:SS / MM:SS" 로 정지.
        // 사용자가 창 X 또는 ⏸(현재 ▶로 표시) 로 종료.
        break;
    }
}

// 리허설 창의 X(close) → ControlWindow 의 ▶ 상태를 강제 리셋.
//  resetPreviewSim 내부 emit previewPlayingChanged(false) 가 다시
//  onPreviewPlayingChanged(false) 를 호출하지만 hide()·applyScene({}) 는 idempotent.
bool Application::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_rehearsalWindow.get() && event->type() == QEvent::Close) {
        if (m_controlWindow) m_controlWindow->resetPreviewSim();
    }
    return QObject::eventFilter(obj, event);
}

} // namespace uwp
