#include "Application.h"

#include "windows/ControlWindow.h"
#include "windows/DisplaySettingsDialog.h"
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
#include "program/PageListWidget.h"
#include "editor/PageProperties.h"
#include "editor/ProgramProperties.h"
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
#include <QDateTime>
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
#include <QProcess>
#include <QPointer>
#include <QRadioButton>
#include <QScreen>
#include <QSettings>
#include <QStatusBar>
#include <QStringList>
#include <QTimer>
#include <QFile>
#include <QPixmap>
#include <QVBoxLayout>
#include <QDebug>

namespace uwp {

// 전방 선언 — 아래 정의된 헬퍼가 앞쪽 람다에서도 참조 가능하도록.
static int pageIndexOf(const Program& p, const QString& pageId);

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
    // ControlWindow 를 닫으면 앱이 종료되도록 — LiveWindow/rehearsal 는
    // quit 판정에서 제외. (기본 quitOnLastWindowClosed 는 모든 top-level 창을
    // 카운트해서, LiveWindow 가 살아있으면 앱 프로세스가 남아 있게 된다.)
    m_liveWindow->setAttribute(Qt::WA_QuitOnClose, false);

    // 리허설(미리보기 재생) 창 — Control 모니터에 뜨는 non-fullscreen LiveWindow.
    // 캔버스 비율 유지하며 폭 800px 상한, 높이 600px 상한.
    m_rehearsalWindow = std::make_unique<LiveWindow>(m_playerPool.get());
    m_rehearsalWindow->setAttribute(Qt::WA_QuitOnClose, false);
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
    connect(m_controlWindow.get(), &ControlWindow::blackoutRequested,
            this, &Application::onBlackoutRequested);
    connect(m_controlWindow.get(), &ControlWindow::playPauseRequested,
            this, &Application::onPlayPauseRequested);

    connect(m_controlWindow.get(), &ControlWindow::selectOutputMonitorRequested,
            this, &Application::onSelectOutputMonitorRequested);
    connect(m_controlWindow.get(), &ControlWindow::displaySettingsRequested,
            this, &Application::onDisplaySettingsRequested);
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
                    if (id == m_editProgramId) {
                        // 편집 중 프로그램의 이름 변경 → Program 헤더(접힌 상태)와
                        // Page 탭 컨텍스트 라벨 둘 다 최신화.
                        pl->setCurrentProgramName(p->name);
                        if (auto* pgl = m_controlWindow->pageList()) {
                            pgl->setProgram(p, dataDir());
                            pgl->setActivePage(m_editPageId);
                        }
                    }
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
    // UI-D Phase B — 페이지 리스트 배선.
    if (auto* pgl = m_controlWindow->pageList()) {
        connect(pgl, &PageListWidget::addRequested,
                this, &Application::onPageAddRequested);
        connect(pgl, &PageListWidget::pageSelected,
                this, &Application::onPageSelected);
        connect(pgl, &PageListWidget::deleteRequested,
                this, &Application::onPageDeleteRequested);
        connect(pgl, &PageListWidget::renameRequested,
                this, &Application::onPageRenameRequested);
        connect(pgl, &PageListWidget::moveUpRequested,
                this, &Application::onPageMoveUpRequested);
        connect(pgl, &PageListWidget::moveDownRequested,
                this, &Application::onPageMoveDownRequested);
        connect(pgl, &PageListWidget::displayTimeEditRequested,
                this, &Application::onPageDisplayTimeEditRequested);
    }
    // 우측 패널 [페이지 속성 · 프로그램 속성] — 컨텍스트 메뉴 대체 인라인 편집.
    if (auto* pp = m_controlWindow->pageProperties()) {
        connect(pp, &PageProperties::renameRequested, this,
                [this](const QString& n){
                    if (!m_editPageId.isEmpty()) onPageRenameRequested(m_editPageId, n);
                });
        connect(pp, &PageProperties::displayTimeChanged, this,
                [this](int sec){
                    if (m_editProgramId.isEmpty() || m_editPageId.isEmpty()) return;
                    const Program* p = m_programs->find(m_editProgramId);
                    if (!p) return;
                    const int idx = pageIndexOf(*p, m_editPageId);
                    if (idx < 0 || p->pages[idx].displayTimeSec == sec) return;
                    Program up = *p;
                    up.pages[idx].displayTimeSec = sec;
                    m_programs->update(up);
                    m_programs->save(resolveProgramsPath());
                    m_controlWindow->setPreviewDisplayTime(sec);
                });
        connect(pp, &PageProperties::moveUpRequested, this,
                [this]{ if (!m_editPageId.isEmpty()) onPageMoveUpRequested(m_editPageId); });
        connect(pp, &PageProperties::moveDownRequested, this,
                [this]{ if (!m_editPageId.isEmpty()) onPageMoveDownRequested(m_editPageId); });
    }
    if (auto* pp = m_controlWindow->programProperties()) {
        connect(pp, &ProgramProperties::renameRequested, this,
                [this](const QString& n){
                    if (!m_editProgramId.isEmpty()) onProgramRenameRequested(m_editProgramId, n);
                });
        connect(pp, &ProgramProperties::endActionChanged, this,
                [this](EndAction ea){
                    if (m_editProgramId.isEmpty()) return;
                    const Program* p = m_programs->find(m_editProgramId);
                    if (!p || p->endAction == ea) return;
                    Program up = *p;
                    up.endAction = ea;
                    m_programs->update(up);
                    m_programs->save(resolveProgramsPath());
                });
        connect(pp, &ProgramProperties::moveUpRequested,   this,
                [this]{ moveEditProgram(-1); });
        connect(pp, &ProgramProperties::moveDownRequested, this,
                [this]{ moveEditProgram(+1); });
    }
    // 부재 시 빈 리스트로 시작(에러 아님). 손상 시 .bak 백업 후 빈 리스트.
    m_programs->load(resolveProgramsPath());
    restoreSessionState();   // 마지막 편집 프로그램 복원 (있으면 자동 선택)

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
        // 저장된 출력 모드가 스크린 좌표면 openProgramProjector 첫 호출부터
        // 그 geometry 로 열리도록 미리 설정. (모니터 모드면 아무 것도 안 함)
        if (m_settings.outputMode() == QStringLiteral("screen")) {
            m_obsProc->setProjectorGeometry(
                m_settings.outputX(),  m_settings.outputY(),
                m_settings.canvasWidth(), m_settings.canvasHeight());
        }
        // 4096 캔버스 상한 우회 — OBS 시작 전에 프로파일 INI 에 원하는 해상도
        // 를 사전 기록. libobs 코어는 16384 까지 지원하므로 이 경로로 큰 캔버스
        // 지정 가능(obs-websocket 은 여전히 4096 캡이라 라이브 변경만 제한).
        m_obsProc->setInitialCanvas(
            m_settings.canvasWidth(), m_settings.canvasHeight());
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
        // TAKE 발생 → BLACK 자동 해제 (실제 컨텐츠가 Live 에 나가므로).
        if (m_blackoutActive) {
            m_blackoutActive = false;
            m_controlWindow->setBlackoutActive(false);
        }
        if (m_programAdvanceTimer) m_programAdvanceTimer->stop();
        if (m_editProgramId.isEmpty() || !m_programs) return;
        const Program* p = m_programs->find(m_editProgramId);
        if (!p || p->pages.isEmpty()) return;
        m_currentProgramId = m_editProgramId;
        // UI-D Phase C: TAKE 는 "현재 편집 페이지"를 Live 로 → 그 페이지부터
        // 순차 재생 시작. m_currentPageIdx 를 편집 페이지 인덱스로 세팅.
        int idx = 0;
        for (int i = 0; i < p->pages.size(); ++i)
            if (p->pages[i].id == m_editPageId) { idx = i; break; }
        m_currentPageIdx = idx;
        if (auto* pl = m_controlWindow->programList())
            pl->setActiveProgram(m_currentProgramId);
        const int t = p->pages[idx].displayTimeSec;
        if (t > 0 && m_programAdvanceTimer)
            m_programAdvanceTimer->start(t * 1000);
        // TAKE 는 "지금 이 씬 나가" — 재생 상태로 확정, 일시정지 해제.
        m_playbackPaused = false;
        m_controlWindow->setLiveState(ControlWindow::LiveState::Playing);
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
    m_controlWindow->showMaximized();   // 처음 실행 시 전체화면(최대화)
    if (!useObs) {
        // qt 엔진: outputMode 에 따라 fullscreen 또는 임의 좌표.
        if (m_settings.outputMode() == QStringLiteral("screen")) {
            m_liveWindow->showAtGeometry(
                m_settings.outputX(),     m_settings.outputY(),
                m_settings.canvasWidth(), m_settings.canvasHeight());
        } else {
            m_liveWindow->showOnMonitor(m_settings.outputMonitorIndex());
        }
    } else {
        qInfo() << "engine=obs — Live output via OBS projector "
                   "(LiveWindow hidden)";
    }

    // (Phase 1 잔재였던 test_video 자동 재생은 프로그램/페이지 기반 시스템
    //  완성 후 제거 — Live 헤더 상태와 어긋나고 실사용에 방해. 필요하면
    //  도구 → 테스트 영상 재생 메뉴 또는 UWP_AUTOTAKE=1 환경변수 사용.)

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

    // 3) OBS 경로에서 숨겨져 있던 LiveWindow 표시 — outputMode 따라 dispatch.
    if (m_settings.outputMode() == QStringLiteral("screen")) {
        m_liveWindow->showAtGeometry(
            m_settings.outputX(),     m_settings.outputY(),
            m_settings.canvasWidth(), m_settings.canvasHeight());
    } else {
        m_liveWindow->showOnMonitor(m_settings.outputMonitorIndex());
    }

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

    // 종료 도중 새 take/자동진행/미러 폴링이 발화하는 것 방지 (backend 리셋
    // 후 dangling 접근 위험).
    if (m_programAdvanceTimer) m_programAdvanceTimer->stop();
    if (m_liveMirrorTimer)     m_liveMirrorTimer->stop();
    if (m_editSaveTimer)       m_editSaveTimer->stop();

    if (m_liveWindow) m_liveWindow->stopVideo();
    if (m_scene) {
        SceneSerializer::saveScene(*m_scene, resolveScenePath());
    }
    if (!m_settingsPath.isEmpty()) {
        m_settings.save(m_settingsPath);   // media_dir 등 보존
    }

    // OBS teardown 은 여기서(=aboutToQuit 콜백, 이벤트 루프 아직 살아있음)
    // 명시적으로 처리한다. Application 소멸자로 미루면 QCoreApplication
    // exec 루프가 이미 리턴된 상태에서 QProcess 내부 notifier 콜백이
    // wrong-thread 로 dispatch 되어 "Timers cannot be stopped from another
    // thread" 경고가 로그 마지막 줄로 찍히는 케이스를 재현했음(P3 조사).
    // stop() 은 client close → kill → waitForFinished(3s) 를 이벤트 루프
    // 활성 상태에서 순서대로 처리하므로 소멸자에서 하던 것과 안전성 차이가 큼.
#if defined(UWP_HAS_OBS)
    if (m_obsProc) {
        m_obsProc->stop();
    }
    m_obsBackend.reset();   // 이후 소멸자엔 남은 게 없어 no-op
    m_obsProc.reset();
#endif
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

// 디스플레이 설정 — 모드 선택형 (monitor / screen).
//   웨딩홀 LED 세팅용. 스크린 모드에서는 startX/Y + W×H 로 데스크톱 임의
//   사각형에 정확히 송출(OBS windowed projector).
void Application::onDisplaySettingsRequested() {
    // 현재값 → 다이얼로그 초기화
    const QString curMode = m_settings.outputMode();
#if defined(UWP_HAS_OBS)
    const bool obsActive =
        (m_settings.engine().compare(QLatin1String("obs"),
             Qt::CaseInsensitive) == 0) && !m_qtFallbackActive;
    const int curMonIdx = obsActive ? m_settings.obs().projectorMonitor
                                    : m_settings.outputMonitorIndex();
#else
    const int curMonIdx = m_settings.outputMonitorIndex();
#endif
    const QRect curGeo(m_settings.outputX(), m_settings.outputY(),
                       m_settings.canvasWidth(), m_settings.canvasHeight());

    DisplaySettingsDialog dlg(m_controlWindow.get(),
                              curMode, curMonIdx, curGeo);
    if (dlg.exec() != QDialog::Accepted) return;

    const QString newMode = dlg.mode();
    const int     newMon  = dlg.monitorIndex();
    const QRect   newGeo  = dlg.outputGeometry();  // 모니터 모드에서도 모니터 실제 사각형 채워 반환

    // ---- 스크린(캔버스) 해상도 라이브 반영 --------------------
    // 두 모드 모두 최종 W×H 가 SceneModel/LiveWindow/OBS 백엔드에 반영되어야
    // Preview·Live 축척이 맞음. 좌표 X/Y 는 프로젝터 배치용.
    const QSize oldCanvas = m_settings.canvasSize();
    const QSize newCanvas(newGeo.width(), newGeo.height());
    if (newCanvas != oldCanvas) {
        m_settings.setCanvasSize(newCanvas.width(), newCanvas.height());
        if (m_scene) m_scene->setCanvasSize(newCanvas);
        if (m_liveWindow)
            m_liveWindow->setCanvasSize(newCanvas.width(), newCanvas.height());
        if (m_rehearsalWindow)
            m_rehearsalWindow->setCanvasSize(newCanvas.width(), newCanvas.height());
#if defined(UWP_HAS_OBS)
        if (m_obsBackend)
            m_obsBackend->setCanvasSize(newCanvas.width(), newCanvas.height());
        // 4096 상한 우회: 다음 OBS 기동 때 INI 로 반영되도록 값 갱신.
        // 라이브 SetVideoSettings 는 4096 이하만 성공(위에서 처리), 초과분은
        // 다음 세션에서 INI 로 적용.
        if (m_obsProc)
            m_obsProc->setInitialCanvas(newCanvas.width(), newCanvas.height());
#endif
    }

    // ---- 모드/좌표/모니터 반영 (엔진별 분기) -------------------
    m_settings.setOutputMode(newMode);
    m_settings.setOutputOrigin(newGeo.x(), newGeo.y());
    m_settings.setOutputMonitorIndex(newMon);
#if defined(UWP_HAS_OBS)
    m_settings.setObsProjectorMonitor(newMon);
#endif

    bool outputChanged = false;
#if defined(UWP_HAS_OBS)
    if (obsActive && m_obsProc) {
        if (newMode == QStringLiteral("screen")) {
            m_obsProc->setProjectorGeometry(
                newGeo.x(), newGeo.y(), newGeo.width(), newGeo.height());
        } else {
            m_obsProc->setProjectorMonitor(newMon);
        }
        outputChanged = true;
    } else
#endif
    {
        // qt 백엔드: mode 에 따라 fullscreen(모니터) 또는 임의 좌표 라이브 반영.
        if (m_liveWindow) {
            if (newMode == QStringLiteral("screen")) {
                m_liveWindow->showAtGeometry(
                    newGeo.x(), newGeo.y(), newGeo.width(), newGeo.height());
            } else {
                m_liveWindow->showOnMonitor(newMon);
            }
            outputChanged = true;
        }
    }

    m_settings.save(m_settingsPath);

    // ---- 상태표시줄 안내 --------------------------------------
    QStringList notes;
    if (newCanvas != oldCanvas)
        notes << tr("해상도 %1×%2").arg(newCanvas.width()).arg(newCanvas.height());
    if (newMode == QStringLiteral("screen"))
        notes << tr("스크린 %1,%2").arg(newGeo.x()).arg(newGeo.y());
    else
        notes << tr("모니터 %1번").arg(newMon);
    m_controlWindow->setStatusText(
        tr("디스플레이 적용: %1").arg(notes.join(QStringLiteral(" · "))));

    // 프로젝터 재오픈 후 현재 씬을 재커밋 → HWND 재바인딩/재적용.
    if (outputChanged && m_takeController) m_takeController->take();

    // 캔버스 크기가 바뀐 경우, 4096 초과분은 OBS 프로파일 INI 로 저장돼
    // 다음 기동에 반영된다(라이브 SetVideoSettings 는 4096 캡). 사용자에게
    // 재시작 여부를 물어보고 예 선택 시 자동 재시작한다.
    if (newCanvas != oldCanvas) {
        const auto reply = QMessageBox::question(
            m_controlWindow.get(),
            tr("재시작이 필요합니다"),
            tr("디스플레이 설정 변경을 완전히 반영하려면 재시작이 필요합니다.\n"
               "지금 재시작하시겠습니까?"),
            QMessageBox::Yes | QMessageBox::No,
            QMessageBox::Yes);
        if (reply == QMessageBox::Yes) {
            // 자식 프로세스로 자신을 다시 실행한 뒤 종료 → 자식은 main.cpp
            // 단일 인스턴스 락 재시도 로직(최대 3초)으로 부모 exit 대기.
            const QString exe = QCoreApplication::applicationFilePath();
            QProcess::startDetached(exe, QStringList{});
            qInfo() << "Application: restart requested — launching child and quitting";
            QMetaObject::invokeMethod(qApp, &QCoreApplication::quit,
                                      Qt::QueuedConnection);
        }
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

// ---- Phase 5b — Program 동작 -----------------------------------
// [+ Add] = 빈 Program 생성 + 편집 대상으로 바인딩. 이후 Preview 편집은
// 디바운스되어 이 program 에 자동 저장(layers + 썸네일).
void Application::onProgramAddRequested() {
    flushEditSave();   // 직전 편집 대상 저장 확정

    Program p;
    p.id        = m_programs->makeUniqueId();
    p.name      = tr("Program %1").arg(m_programs->count() + 1);
    p.endAction = EndAction::Hold;            // 자동진행 기본=정지(안전, D5)
    // 기본 페이지 1개 (Program{} 초기화로 이미 존재) 에 UUID 발번.
    p.pages.first().id = ProgramRepository::makePageId();

    // Preview 를 비워 편집 시작점으로. (로드성 변경이므로 자동저장 억제)
    m_suppressEditSave = true;
    m_scene->clear();
    m_suppressEditSave = false;

    // 빈 캔버스 썸네일 1회 렌더 — 프로그램 대표 = 첫 페이지 썸네일과 동일 파일.
    const QString thumbsRel = QStringLiteral("programs/thumbs");
    QDir().mkpath(dataDir() + "/" + thumbsRel);
    if (auto* pc = m_controlWindow->previewCanvas()) {
        const QSize cs = m_scene->canvasSize();
        const int tw = 320;
        const int th = (cs.width() > 0)
            ? qMax(1, qRound(320.0 * cs.height() / cs.width())) : 180;
        const QPixmap pm  = pc->renderThumbnail(QSize(tw, th));
        const QString rel = thumbsRel + "/" + p.id + ".png";
        if (!pm.isNull() && pm.save(dataDir() + "/" + rel, "PNG")) {
            p.thumbnailRelPath          = rel;
            p.pages.first().thumbnailRelPath = rel;
        }
    }

    m_programs->add(p);                       // → programAdded → 리스트 갱신
    m_programs->save(resolveProgramsPath());

    m_editProgramId = p.id;                    // 이후 편집은 이 program 에 저장
    m_editPageId    = p.pages.first().id;
    persistSessionState();                     // 세션 복원용
    if (auto* pl = m_controlWindow->programList()) {
        pl->selectProgram(p.id);
        pl->setCurrentProgramName(p.name);     // 접힌 상태 헤더 라벨(수동 접기 시 사용)
        // 자동 접기 하지 않음 — 사용자가 다른 프로그램의 페이지도 비교하며
        // 볼 수 있도록 프로그램 리스트를 그대로 노출.
    }
    if (auto* pgl = m_controlWindow->pageList()) {
        pgl->setProgram(&p, dataDir());
        pgl->setActivePage(m_editPageId);
    }
    m_controlWindow->setPreviewDisplayTime(p.pages.first().displayTimeSec);
    m_controlWindow->setStatusText(
        tr("Added (editing): %1 — 미디어를 배치하면 자동 저장됩니다").arg(p.name));
}

void Application::onProgramSelected(const QString& id) {
    flushEditSave();                           // 이전 편집 대상 저장 확정
    const Program* p = m_programs->find(id);
    if (!p) return;
    m_suppressEditSave = true;
    // 프로그램 로드 = 그 프로그램의 첫 페이지를 편집 대상으로. 페이지 스위칭은
    // 이후 onPageSelected 가 담당.
    m_scene->replaceAll(p->pages.first().layers);
    m_suppressEditSave = false;
    m_editProgramId = id;
    m_editPageId    = p->pages.first().id;
    persistSessionState();                     // 세션 복원용
    if (auto* pl = m_controlWindow->programList()) {
        pl->setCurrentProgramName(p->name);
        // 자동 접기 안 함 — 사용자가 다른 프로그램들의 페이지도 살펴보며
        // 편집 대상을 스위치할 수 있도록 유지. 접기/펼치기는 ∧/∨ 버튼으로 수동.
    }
    if (auto* pgl = m_controlWindow->pageList()) {
        pgl->setProgram(p, dataDir());
        pgl->setActivePage(m_editPageId);
    }
    if (auto* pp = m_controlWindow->programProperties()) {
        const int pIdx = m_programs->indexOf(id);
        pp->setProgram(p, pIdx, m_programs->programs().size());
    }
    if (auto* pp = m_controlWindow->pageProperties())
        pp->setPage(&p->pages.first(), 0, p->pages.size());
    m_controlWindow->setPreviewDisplayTime(p->pages.first().displayTimeSec);
    m_controlWindow->setStatusText(tr("Loaded: %1").arg(p->name));
}

void Application::onProgramPlayRequested(const QString& id) {
    playProgram(id);   // 수동 Play → 공통 경로(자동 진행 타이머 포함)
}

// ---- Phase 5c + UI-D Phase C — 재생 / 페이지 순회 / 자동 진행 --
// 수동 Play(카드 자동 선택 후 TAKE) 와 자동 진행이 공유하는 단일 경로.
// UI-D 이후: 프로그램은 여러 페이지를 순차 재생, 마지막 페이지 종료 시
//   program.endAction 이 다음 프로그램 여부를 결정.
void Application::playProgram(const QString& id) {
    flushEditSave();
    const Program* p = m_programs->find(id);
    if (!p || p->pages.isEmpty()) return;

    m_editProgramId    = id;
    m_currentProgramId = id;            // take 전에 설정 → currentNovaPresetId 정확
    m_editPageId       = p->pages.first().id;
    persistSessionState();              // 세션 복원용

    if (auto* pl = m_controlWindow->programList()) {
        pl->setActiveProgram(id);       // 재생중 강조
        pl->selectProgram(id);
    }
    if (auto* pgl = m_controlWindow->pageList()) {
        pgl->setProgram(p, dataDir());
        pgl->setActivePage(m_editPageId);
    }
    m_controlWindow->setStatusText(tr("Playing: %1").arg(p->name));
    m_playbackPaused = false;
    m_controlWindow->setLiveState(ControlWindow::LiveState::Playing);

    playPageAt(p, 0);                   // 첫 페이지부터 재생
}

// 지정 페이지를 SceneModel 로 로드 → Live 송출 → 페이지 displayTime 으로 타이머.
//   재생 중 사용자 편집 대상(m_editPageId)도 이 페이지로 이동해서 편집자가
//   화면에서 현재 뭐가 나오는지 즉시 파악할 수 있게 함.
void Application::playPageAt(const Program* p, int pageIdx) {
    if (!p || pageIdx < 0 || pageIdx >= p->pages.size()) return;
    m_currentPageIdx = pageIdx;
    m_editPageId     = p->pages[pageIdx].id;

    m_suppressEditSave = true;
    m_scene->replaceAll(p->pages[pageIdx].layers);
    m_suppressEditSave = false;

    if (m_takeController) m_takeController->take();   // Live 송출 (+ taken→NovaStar)

    if (auto* pgl = m_controlWindow->pageList())
        pgl->setActivePage(m_editPageId);

    const int t = p->pages[pageIdx].displayTimeSec;
    m_controlWindow->setPreviewDisplayTime(t);
    // pause/resume elapsed 계산용 기준 시각. displayTime=0 이면 자동 진행
    // 없으므로 pause 개념도 사실상 무의미하지만 기록해두면 무해.
    m_pageStartMs   = QDateTime::currentMSecsSinceEpoch();
    m_pauseRemainMs = 0;
    if (m_programAdvanceTimer) {
        m_programAdvanceTimer->stop();
        if (t > 0) {
            m_programAdvanceTimer->start(t * 1000);
            qInfo() << "Page advance armed:" << t << "s,"
                    << "program=" << p->name
                    << "pageIdx=" << pageIdx
                    << "of" << p->pages.size();
        }
    }
}

// 페이지 만료 → 다음 페이지 or 프로그램 종료 동작.
void Application::onProgramAdvance() {
    if (m_currentProgramId.isEmpty()) return;
    const Program* cur = m_programs->find(m_currentProgramId);
    if (!cur || cur->pages.isEmpty()) return;

    // 다음 페이지 있으면 진행.
    const int nextIdx = m_currentPageIdx + 1;
    if (nextIdx < cur->pages.size()) {
        playPageAt(cur, nextIdx);
        return;
    }

    // 마지막 페이지 → program.endAction.
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
        playProgram(curId);             // 자기 프로그램 첫 페이지부터 재생
        break;
    case EndAction::Stop:
        stopProgramPlayback();
        break;
    case EndAction::First: {
        if (m_programs->count() > 0)
            playProgram(m_programs->programs().first().id);
        else
            stopProgramPlayback();
        break;
    }
    case EndAction::Hold:
    default:
        break;                          // Live 유지 (마지막 페이지), 타이머 정지
    }
}

void Application::stopProgramPlayback() {
    if (m_programAdvanceTimer) m_programAdvanceTimer->stop();
    if (m_takeController) m_takeController->clearLive();   // Live 비움(검정), 편집 무영향
    m_currentProgramId.clear();
    m_currentPageIdx = 0;
    if (auto* pl = m_controlWindow->programList())
        pl->setActiveProgram(QString());
    m_controlWindow->setPreviewDisplayTime(-1);            // UI-F
    m_playbackPaused = false;
    m_controlWindow->setLiveState(ControlWindow::LiveState::Idle);
    m_controlWindow->setStatusText(tr("Program stopped"));
}

// 클러스터 ▶/⏸ 토글 — 컨텍스트별 재생/일시정지/재개.
//   대기       → 편집중 프로그램 재생 시작 (blackout ON 이면 자동 해제)
//   재생중     → 일시정지: 자동 진행 timer stop + 잔여 시간 저장 + 영상 정지
//   일시정지  → 재개: 저장된 잔여 시간으로 timer 재시작 + 영상 재개
void Application::onPlayPauseRequested() {
    auto ensurePowerOn = [this]() {
        if (m_blackoutActive) {
            m_blackoutActive = false;
            m_controlWindow->setBlackoutActive(false);
        }
    };

    // (1) 재생중 → 일시정지
    if (!m_currentProgramId.isEmpty() && !m_playbackPaused) {
        // 현재 페이지의 남은 시간 계산 (auto-advance timer 기반).
        // 무자동진행(displayTime=0) 또는 타이머 없으면 0 → 재개도 0 유지(자동 진행 없음).
        int remainMs = 0;
        if (m_programAdvanceTimer && m_programAdvanceTimer->isActive())
            remainMs = m_programAdvanceTimer->remainingTime();
        else if (m_programAdvanceTimer)
            remainMs = 0;   // 이미 만료됐거나 시작 안 함
        m_pauseRemainMs = qMax(0, remainMs);

        if (m_programAdvanceTimer) m_programAdvanceTimer->stop();
        // Qt 엔진: 영상 레이어를 현재 프레임에 정지 → 사용자가 시각적으로 인지.
        if (m_liveWindow) m_liveWindow->pauseAllVideos();
        // (OBS 엔진의 ffmpeg_source pause 는 추후 obs-websocket
        //  TriggerMediaInputAction 로 확장 가능 — 지금은 타이머만 정지.)

        m_playbackPaused = true;
        m_controlWindow->setLiveState(ControlWindow::LiveState::Paused);
        m_controlWindow->setStatusText(
            tr("일시정지 — 남은 %1초").arg(m_pauseRemainMs / 1000));
        return;
    }

    // (2) 일시정지 → 재개 (저장된 남은 시간으로 재시작 + 영상 재개)
    if (m_playbackPaused && !m_currentProgramId.isEmpty()) {
        ensurePowerOn();
        m_playbackPaused = false;
        m_controlWindow->setLiveState(ControlWindow::LiveState::Playing);
        // 영상 재개 → 정지된 프레임부터 이어짐.
        if (m_liveWindow) m_liveWindow->resumeAllVideos();
        // 자동 진행 타이머 재무장 — 저장한 남은 시간부터. 0 이면 자동 진행 없음.
        if (m_programAdvanceTimer && m_pauseRemainMs > 0) {
            m_programAdvanceTimer->start(m_pauseRemainMs);
            qInfo() << "Page advance resumed:" << m_pauseRemainMs << "ms remaining";
            // 기준 시각을 "지금 - (원래 duration - 남은시간)" 로 재조정 → 이후 재-pause 계산 정확.
            const Program* p = m_programs->find(m_currentProgramId);
            if (p && m_currentPageIdx >= 0 && m_currentPageIdx < p->pages.size()) {
                const int totalMs = p->pages[m_currentPageIdx].displayTimeSec * 1000;
                const qint64 elapsed = qint64(totalMs) - qint64(m_pauseRemainMs);
                m_pageStartMs = QDateTime::currentMSecsSinceEpoch() - elapsed;
            }
        }
        m_controlWindow->setStatusText(tr("재개"));
        return;
    }

    // (3) 대기 → 편집중 프로그램 시작
    if (m_editProgramId.isEmpty()) {
        m_controlWindow->setStatusText(
            tr("재생할 프로그램이 선택되어 있지 않습니다."));
        m_controlWindow->setLiveState(ControlWindow::LiveState::Idle);
        return;
    }
    ensurePowerOn();
    playProgram(m_editProgramId);
}

// ---- Phase 5b — 편집 자동저장 (현재 편집 대상 program) ----------
void Application::scheduleEditSave() {
    if (m_suppressEditSave || m_editProgramId.isEmpty()) return;
    if (m_editSaveTimer) m_editSaveTimer->start();   // 디바운스 재시작
}

// 편집 대상 프로그램 안 현재 페이지(m_editPageId) 인덱스. -1 = 없음.
static int pageIndexOf(const Program& p, const QString& pageId) {
    for (int i = 0; i < p.pages.size(); ++i)
        if (p.pages[i].id == pageId) return i;
    return -1;
}

void Application::persistEditProgram() {
    if (m_editProgramId.isEmpty()) return;
    const Program* cur = m_programs->find(m_editProgramId);
    if (!cur) {
        m_editProgramId.clear();
        m_editPageId.clear();
        persistSessionState();
        return;
    }
    Program up = *cur;

    // 편집 페이지 인덱스 확정 (없으면 첫 페이지로 안전 폴백).
    int pIdx = pageIndexOf(up, m_editPageId);
    if (pIdx < 0) {
        pIdx = 0;
        if (!up.pages.isEmpty()) m_editPageId = up.pages.first().id;
    }
    up.pages[pIdx].layers = m_scene->layers();

    if (auto* pc = m_controlWindow->previewCanvas()) {
        const QSize cs = m_scene->canvasSize();
        const int tw = 320;
        const int th = (cs.width() > 0)
            ? qMax(1, qRound(320.0 * cs.height() / cs.width())) : 180;
        const QPixmap pm = pc->renderThumbnail(QSize(tw, th));
        // 페이지 썸네일 파일: programs/thumbs/<progId>_<pageId>.png
        QString pageRel = up.pages[pIdx].thumbnailRelPath;
        if (pageRel.isEmpty())
            pageRel = QStringLiteral("programs/thumbs/") + up.id
                    + "_" + up.pages[pIdx].id + ".png";
        QDir().mkpath(QFileInfo(dataDir() + "/" + pageRel).absolutePath());
        if (!pm.isNull() && pm.save(dataDir() + "/" + pageRel, "PNG"))
            up.pages[pIdx].thumbnailRelPath = pageRel;
        // 프로그램 대표 썸네일 = 첫 페이지의 썸네일 파일 그대로.
        //   (편집한 페이지가 첫 페이지가 아니어도 프로그램 카드는 여전히 첫
        //    페이지 이미지로 보여야 하므로 여기서 갱신하지 않음. 첫 페이지
        //    편집 시에만 두 경로가 같은 파일을 가리키게 됨.)
        if (pIdx == 0) up.thumbnailRelPath = pageRel;
    }

    m_programs->update(up);                    // → programUpdated → 리스트 갱신
    m_programs->save(resolveProgramsPath());
    // pageList 재구성은 programUpdated 시그널 핸들러가 처리 (중복 갱신 회피).
}

void Application::flushEditSave() {
    if (m_editSaveTimer && m_editSaveTimer->isActive()) {
        m_editSaveTimer->stop();
        persistEditProgram();
    }
}

// 세션 상태(마지막 편집 프로그램 id) 를 QSettings 로 저장.
//   settings.json 은 앱 설정용이라 자주 변경되는 세션 상태는 QSettings 분리.
void Application::persistSessionState() {
    QSettings qs(QStringLiteral("Hanmac"), QStringLiteral("uWeddingPlayer"));
    qs.setValue(QStringLiteral("session/lastEditProgramId"), m_editProgramId);
}

// 앱 시작 시(m_programs 로드 완료 후) 마지막 편집 프로그램을 복원.
//   목록에 존재하지 않는 id (삭제됨)는 무시. onProgramSelected() 를 재사용해
//   씬/페이지/카드 강조까지 일관되게 복원.
void Application::restoreSessionState() {
    QSettings qs(QStringLiteral("Hanmac"), QStringLiteral("uWeddingPlayer"));
    const QString lastId = qs.value(
        QStringLiteral("session/lastEditProgramId")).toString();
    if (lastId.isEmpty()) return;
    if (!m_programs->find(lastId)) return;    // 삭제된 프로그램이면 조용히 무시
    onProgramSelected(lastId);
    // 프로그램 카드 리스트에서도 시각 선택 상태 반영 (유저 클릭과 동일 UI 상태).
    if (auto* pl = m_controlWindow->programList())
        pl->selectProgram(lastId);
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
    // 확인 다이얼로그는 여기서 처리 — X 버튼·Del 키 두 경로 통합.
    const QString name  = p->name.isEmpty() ? id : p->name;
    const auto reply = QMessageBox::question(
        m_controlWindow.get(), tr("프로그램 삭제"),
        tr("\"%1\" 을(를) 삭제할까요?").arg(name));
    if (reply != QMessageBox::Yes) return;
    const QString thumb = p->thumbnailRelPath;

    if (!thumb.isEmpty()) QFile::remove(dataDir() + "/" + thumb);
    // 프로그램에 딸린 페이지별 썸네일 정리 (best-effort — 실패해도 무시).
    for (const Page& pg : p->pages) {
        if (!pg.thumbnailRelPath.isEmpty() && pg.thumbnailRelPath != thumb)
            QFile::remove(dataDir() + "/" + pg.thumbnailRelPath);
    }
    if (m_editProgramId == id) {               // 편집 대상이 삭제됨 → 자동저장 중단
        if (m_editSaveTimer) m_editSaveTimer->stop();
        m_editProgramId.clear();
        m_editPageId.clear();
        persistSessionState();
        m_controlWindow->setPreviewDisplayTime(-1);   // UI-F
        if (auto* pgl = m_controlWindow->pageList())
            pgl->setProgram(nullptr, dataDir());
    }
    if (m_currentProgramId == id) {
        m_currentProgramId.clear();
        if (auto* pl = m_controlWindow->programList())
            pl->setActiveProgram(QString());
    }
    m_programs->remove(id);                    // → programRemoved → 갱신
    m_programs->save(resolveProgramsPath());
    m_controlWindow->setStatusText(tr("삭제됨: %1").arg(name));
}

// ▶ 클릭(true) / ⏸·리셋(false) 에 반응해 리허설 창 제어.
//   playing=true  : 현재 SceneModel 스냅샷을 리허설 창에 apply + 창 표시
//                   + 리허설 세션 트래킹용 m_previewProgramId/Idx 시작값 설정
//   playing=false : 창 숨김 + 씬 비움(플레이어 반환) + 세션 트래킹 클리어
// 자연 만료(auto-complete)는 previewCompleted 로 별도 처리 — 페이지 순회 →
// 마지막 페이지 후 program.endAction 체인.
void Application::onPreviewPlayingChanged(bool playing) {
    if (!m_rehearsalWindow) return;
    if (playing) {
        // 편집 중 상태를 그대로 스냅샷 — Take 와 달리 Live 는 무영향.
        m_rehearsalWindow->applyScene(m_scene->layers());
        m_rehearsalWindow->show();
        m_rehearsalWindow->raise();
        m_rehearsalWindow->activateWindow();
        // 리허설 세션 시작 — 현재 편집 페이지에서 시작(전체 프로그램 순회).
        m_previewProgramId = m_editProgramId;
        m_previewPageIdx   = 0;
        if (!m_previewProgramId.isEmpty()) {
            const Program* p = m_programs->find(m_previewProgramId);
            if (p) {
                for (int i = 0; i < p->pages.size(); ++i)
                    if (p->pages[i].id == m_editPageId) {
                        m_previewPageIdx = i; break;
                    }
            }
        }
    } else {
        m_rehearsalWindow->applyScene({});      // 플레이어 반환
        m_rehearsalWindow->hide();
        m_previewProgramId.clear();
        m_previewPageIdx = 0;
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

    // 다음 페이지 있으면 페이지 순회 (같은 프로그램 유지).
    const int nextPageIdx = m_previewPageIdx + 1;
    if (nextPageIdx < p->pages.size()) {
        m_previewPageIdx = nextPageIdx;
        m_rehearsalWindow->applyScene(p->pages[nextPageIdx].layers);
        m_controlWindow->restartPreviewCountdown(
            p->pages[nextPageIdx].displayTimeSec);
        return;
    }

    // 마지막 페이지 종료 → program.endAction 으로 체인.
    switch (p->endAction) {
    case EndAction::Loop:
        // 같은 프로그램 첫 페이지부터 다시 (페이지 인덱스 리셋).
        m_previewPageIdx = 0;
        m_rehearsalWindow->applyScene(p->pages.first().layers);
        m_controlWindow->restartPreviewCountdown(
            p->pages.first().displayTimeSec);
        break;
    case EndAction::Next: {
        const QString nextId = m_programs->nextIdAfter(m_previewProgramId,
                                                        EndAction::Next);
        if (nextId.isEmpty()) { finishSession(); break; }   // 마지막 → stop
        const Program* np = m_programs->find(nextId);
        if (!np || np->pages.isEmpty()) { finishSession(); break; }
        m_previewProgramId = nextId;
        m_previewPageIdx   = 0;
        m_rehearsalWindow->applyScene(np->pages.first().layers);
        m_controlWindow->restartPreviewCountdown(
            np->pages.first().displayTimeSec);
        break;
    }
    case EndAction::First: {
        if (m_programs->count() == 0) { finishSession(); break; }
        const Program& fp = m_programs->programs().first();
        if (fp.pages.isEmpty()) { finishSession(); break; }
        m_previewProgramId = fp.id;
        m_previewPageIdx   = 0;
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

// ---- UI-D Phase B — 페이지 관리 슬롯 ---------------------------
// 편집 대상 프로그램 안에서 페이지를 추가/스위치/삭제/이름변경/순서변경.
// 공통 패턴:
//   1) 편집 대상 프로그램 존재 확인 (m_editProgramId)
//   2) 사본 up 을 만들어 pages 조작
//   3) m_programs->update(up) + save
//   4) PageListWidget 재구성 + setActivePage
//   5) 필요 시 SceneModel 재로드 + Preview 툴바 시간 갱신

void Application::onPageAddRequested() {
    if (m_editProgramId.isEmpty()) return;
    flushEditSave();   // 현재 페이지 편집 확정
    const Program* p = m_programs->find(m_editProgramId);
    if (!p) return;
    Program up = *p;
    Page pg;
    pg.id = ProgramRepository::makePageId();
    // 새 페이지는 빈 layers + displayTime 0(수동). 이름 미지정 → 카드에 순번.
    up.pages.push_back(pg);
    m_programs->update(up);
    m_programs->save(resolveProgramsPath());

    // 새 페이지를 편집 대상으로 자동 스위치 — 편집자가 바로 채워넣기 가능.
    m_editPageId = pg.id;
    m_suppressEditSave = true;
    m_scene->clear();
    m_suppressEditSave = false;
    m_controlWindow->setPreviewDisplayTime(pg.displayTimeSec);
    if (auto* pgl = m_controlWindow->pageList()) {
        const Program* np = m_programs->find(m_editProgramId);
        if (np) { pgl->setProgram(np, dataDir()); pgl->setActivePage(m_editPageId); }
    }
    m_controlWindow->setStatusText(
        tr("페이지 추가됨 (%1)").arg(up.pages.size()));
}

void Application::onPageSelected(const QString& pageId) {
    if (m_editProgramId.isEmpty() || pageId == m_editPageId) return;
    flushEditSave();   // 이전 페이지 편집 확정 (beforeLeaveCurrentPage 패턴)
    const Program* p = m_programs->find(m_editProgramId);
    if (!p) return;
    const int idx = pageIndexOf(*p, pageId);
    if (idx < 0) return;

    m_editPageId = pageId;
    m_suppressEditSave = true;
    m_scene->replaceAll(p->pages[idx].layers);
    m_suppressEditSave = false;
    m_controlWindow->setPreviewDisplayTime(p->pages[idx].displayTimeSec);
    if (auto* pgl = m_controlWindow->pageList())
        pgl->setActivePage(m_editPageId);
    if (auto* pp = m_controlWindow->pageProperties())
        pp->setPage(&p->pages[idx], idx, p->pages.size());
}

void Application::onPageDeleteRequested(const QString& pageId) {
    if (m_editProgramId.isEmpty()) return;
    const Program* p = m_programs->find(m_editProgramId);
    if (!p) return;
    // 마지막 페이지는 삭제 금지 — invariant(≥1) 유지.
    if (p->pages.size() <= 1) {
        QMessageBox::information(m_controlWindow.get(), tr("페이지 삭제"),
            tr("마지막 페이지는 삭제할 수 없습니다."));
        return;
    }
    const int idx = pageIndexOf(*p, pageId);
    if (idx < 0) return;
    const QString pageName = p->pages[idx].name.isEmpty()
        ? QString::number(idx + 1) : p->pages[idx].name;
    const auto reply = QMessageBox::question(
        m_controlWindow.get(), tr("페이지 삭제"),
        tr("페이지 \"%1\" 을(를) 삭제할까요?").arg(pageName));
    if (reply != QMessageBox::Yes) return;

    // 편집 대상 페이지 삭제 시: 삭제할 페이지의 씬은 폐기 → 자동저장 억제 후
    //   인접(이전 우선, 없으면 다음) 페이지로 자동 스위치.
    const bool wasEdit = (pageId == m_editPageId);
    Program up = *p;
    // 페이지 썸네일 파일 정리 (best-effort).
    if (!up.pages[idx].thumbnailRelPath.isEmpty()
        && up.pages[idx].thumbnailRelPath != up.thumbnailRelPath) {
        QFile::remove(dataDir() + "/" + up.pages[idx].thumbnailRelPath);
    }
    up.pages.remove(idx);
    // 첫 페이지가 삭제되었으면 프로그램 대표 썸네일도 새 첫 페이지로.
    if (idx == 0 && !up.pages.isEmpty())
        up.thumbnailRelPath = up.pages.first().thumbnailRelPath;
    m_programs->update(up);
    m_programs->save(resolveProgramsPath());

    if (wasEdit) {
        const int newIdx = qMin(idx, up.pages.size() - 1);
        m_editPageId = up.pages[newIdx].id;
        m_suppressEditSave = true;
        m_scene->replaceAll(up.pages[newIdx].layers);
        m_suppressEditSave = false;
        m_controlWindow->setPreviewDisplayTime(up.pages[newIdx].displayTimeSec);
    }
    if (auto* pgl = m_controlWindow->pageList()) {
        const Program* np = m_programs->find(m_editProgramId);
        if (np) { pgl->setProgram(np, dataDir()); pgl->setActivePage(m_editPageId); }
    }
}

void Application::onPageRenameRequested(const QString& pageId,
                                         const QString& newName) {
    if (m_editProgramId.isEmpty()) return;
    const Program* p = m_programs->find(m_editProgramId);
    if (!p) return;
    const int idx = pageIndexOf(*p, pageId);
    if (idx < 0) return;
    if (p->pages[idx].name == newName) return;
    Program up = *p;
    up.pages[idx].name = newName;
    m_programs->update(up);
    m_programs->save(resolveProgramsPath());
    if (auto* pgl = m_controlWindow->pageList()) {
        const Program* np = m_programs->find(m_editProgramId);
        if (np) { pgl->setProgram(np, dataDir()); pgl->setActivePage(m_editPageId); }
    }
}

void Application::moveEditProgram(int delta) {
    if (m_editProgramId.isEmpty() || !m_programs) return;
    const int from = m_programs->indexOf(m_editProgramId);
    if (from < 0) return;
    const int to = from + delta;
    const int total = m_programs->programs().size();
    if (to < 0 || to >= total) return;   // 이미 경계
    m_programs->reorder(from, to);
    m_programs->save(resolveProgramsPath());
    // 카드 리스트 재구성은 programsReloaded 시그널 → 기존 핸들러가 처리하지만,
    // ProgramProperties 의 앞/뒤로 버튼 활성 상태는 새 인덱스로 재계산.
    if (auto* pp = m_controlWindow->programProperties()) {
        const Program* np = m_programs->find(m_editProgramId);
        pp->setProgram(np, to, total);
    }
    // 편집중 프로그램의 새 위치를 리스트에서도 시각 반영.
    if (auto* pl = m_controlWindow->programList())
        pl->selectProgram(m_editProgramId);
}

void Application::onPageMoveUpRequested(const QString& pageId) {
    if (m_editProgramId.isEmpty()) return;
    const Program* p = m_programs->find(m_editProgramId);
    if (!p) return;
    const int idx = pageIndexOf(*p, pageId);
    if (idx <= 0) return;   // 이미 맨 위
    Program up = *p;
    std::swap(up.pages[idx - 1], up.pages[idx]);
    // 첫 페이지 스왑 시 프로그램 대표 썸네일 갱신.
    up.thumbnailRelPath = up.pages.first().thumbnailRelPath;
    m_programs->update(up);
    m_programs->save(resolveProgramsPath());
    const Program* np = m_programs->find(m_editProgramId);
    if (!np) return;
    if (auto* pgl = m_controlWindow->pageList()) {
        pgl->setProgram(np, dataDir()); pgl->setActivePage(m_editPageId);
    }
    // 이동 후 새 인덱스로 PageProperties 갱신 — 경계(첫/마지막) 도달 시
    // 앞/뒤 버튼 활성 상태 재계산.
    if (auto* pp = m_controlWindow->pageProperties()) {
        const int newIdx = idx - 1;
        pp->setPage(&np->pages[newIdx], newIdx, np->pages.size());
    }
}

void Application::onPageMoveDownRequested(const QString& pageId) {
    if (m_editProgramId.isEmpty()) return;
    const Program* p = m_programs->find(m_editProgramId);
    if (!p) return;
    const int idx = pageIndexOf(*p, pageId);
    if (idx < 0 || idx + 1 >= p->pages.size()) return;   // 이미 맨 아래
    Program up = *p;
    std::swap(up.pages[idx + 1], up.pages[idx]);
    up.thumbnailRelPath = up.pages.first().thumbnailRelPath;
    m_programs->update(up);
    m_programs->save(resolveProgramsPath());
    const Program* np = m_programs->find(m_editProgramId);
    if (!np) return;
    if (auto* pgl = m_controlWindow->pageList()) {
        pgl->setProgram(np, dataDir()); pgl->setActivePage(m_editPageId);
    }
    if (auto* pp = m_controlWindow->pageProperties()) {
        const int newIdx = idx + 1;
        pp->setPage(&np->pages[newIdx], newIdx, np->pages.size());
    }
}

// UI-D Phase C — 페이지 우클릭 "표시 시간 설정...".
//  Program 시간 편집과 동일 패턴이지만 대상이 페이지. 편집 대상 페이지면
//  Preview 툴바 라벨/▶ 활성 즉시 반영. 재생 중 이 페이지의 시간이 바뀌면
//  현재 타이머 재무장은 하지 않음(현재 페이지의 남은 시간 유지가 자연스러움).
void Application::onPageDisplayTimeEditRequested(const QString& pageId) {
    if (m_editProgramId.isEmpty()) return;
    const Program* p = m_programs->find(m_editProgramId);
    if (!p) return;
    const int idx = pageIndexOf(*p, pageId);
    if (idx < 0) return;
    const int curSec = p->pages[idx].displayTimeSec;
    bool ok = false;
    const int sec = QInputDialog::getInt(
        m_controlWindow.get(),
        tr("페이지 표시 시간"),
        tr("자동 진행 초 (0 = 수동 · 최대 86400):"),
        curSec, 0, 86400, 1, &ok);
    if (!ok || sec == curSec) return;
    Program up = *p;
    up.pages[idx].displayTimeSec = sec;
    m_programs->update(up);
    m_programs->save(resolveProgramsPath());
    // 편집 대상 페이지면 Preview 툴바 라벨/▶ 활성 즉시 갱신.
    if (pageId == m_editPageId)
        m_controlWindow->setPreviewDisplayTime(sec);
}

// Live 헤더 ON/OFF 토글 — 프로젝터 화면 마스크. clearLive 는 사용하지 않아
// 페이지 상태(영상 위치/이미지 경과 시간)를 보존한다.
//   OFF: 자동 진행 timer 남은시간 저장 · 영상 pause · 위젯 hide (검정 마스크)
//   ON : 위젯 show · 영상 resume · timer 남은시간으로 재시작 (자동 재개는 OFF
//        가 자동 pause 를 발생시킨 경우에만; 사용자 ⏸ 상태였다면 마스크만 해제)
void Application::onBlackoutRequested() {
    if (!m_takeController) return;
    m_blackoutActive = !m_blackoutActive;

    if (m_blackoutActive) {
        // ---- OFF ----
        const bool wasPlaying =
            !m_playbackPaused && !m_currentProgramId.isEmpty();
        if (wasPlaying) {
            // 남은 시간 저장 + 자동 진행 타이머 정지 + 영상 pause.
            int remainMs = 0;
            if (m_programAdvanceTimer && m_programAdvanceTimer->isActive())
                remainMs = m_programAdvanceTimer->remainingTime();
            m_pauseRemainMs = qMax(0, remainMs);
            if (m_programAdvanceTimer) m_programAdvanceTimer->stop();
            if (m_liveWindow) m_liveWindow->pauseAllVideos();
            m_playbackPaused    = true;
            m_autoPausedByOff   = true;   // ON 시 자동 resume 표식
        }
        // 화면 마스크 — 위젯을 파괴하지 않고 hide 만 → 페이지 상태 보존.
        if (m_liveWindow) m_liveWindow->setMasked(true);
        m_controlWindow->setLiveState(ControlWindow::LiveState::ScreenOff);
        m_controlWindow->setStatusText(tr("Screen OFF"));
    } else {
        // ---- ON ----
        if (m_liveWindow) m_liveWindow->setMasked(false);
        if (m_autoPausedByOff) {
            // OFF 로 자동 pause 됐던 케이스 → 자동 resume.
            m_autoPausedByOff = false;
            m_playbackPaused  = false;
            if (m_liveWindow) m_liveWindow->resumeAllVideos();
            if (m_programAdvanceTimer && m_pauseRemainMs > 0) {
                m_programAdvanceTimer->start(m_pauseRemainMs);
                qInfo() << "OFF→ON resume:" << m_pauseRemainMs << "ms remaining";
                // pageStartMs 재조정 — 이후 재 pause 계산 정확.
                const Program* p = m_programs->find(m_currentProgramId);
                if (p && m_currentPageIdx >= 0 && m_currentPageIdx < p->pages.size()) {
                    const int totalMs = p->pages[m_currentPageIdx].displayTimeSec * 1000;
                    const qint64 elapsed = qint64(totalMs) - qint64(m_pauseRemainMs);
                    m_pageStartMs = QDateTime::currentMSecsSinceEpoch() - elapsed;
                }
            }
            m_controlWindow->setLiveState(ControlWindow::LiveState::Playing);
        } else {
            // 사용자 ⏸ 상태였거나 idle. 마스크만 해제하고 상태는 유지.
            if (m_currentProgramId.isEmpty())
                m_controlWindow->setLiveState(ControlWindow::LiveState::Idle);
            else
                m_controlWindow->setLiveState(
                    m_playbackPaused ? ControlWindow::LiveState::Paused
                                     : ControlWindow::LiveState::Playing);
        }
        m_controlWindow->setStatusText(tr("Screen ON"));
    }
    m_controlWindow->setBlackoutActive(m_blackoutActive);
}

} // namespace uwp
