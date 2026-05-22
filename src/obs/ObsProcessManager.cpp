#include "ObsProcessManager.h"

#include "ObsClient.h"
#include "app/Settings.h"

#include <QProcess>
#include <QTimer>
#include <QFileInfo>
#include <QDir>
#include <QCoreApplication>
#include <QJsonObject>
#include <QDebug>

#include <functional>
#include <memory>

#ifdef _WIN32
#include <windows.h>
#endif

namespace uwp {

ObsProcessManager::ObsProcessManager(const ObsConfig& cfg, QObject* parent)
    : QObject(parent), m_cfg(cfg) {}

ObsProcessManager::~ObsProcessManager() {
    // 앱 종료 시 OBS 자식 프로세스를 남기지 않는다.
    // kill() 직행: OBS 32.x 는 정상 종료 teardown 에서 크래시하는 버그가 있어
    // WM_CLOSE(terminate) 로 깨끗이 닫게 두면 "OBS has crashed" 창이 뜬다.
    // TerminateProcess 로 teardown 자체를 건너뛴다(상세는 cleanObsCrashState()).
    m_stopRequested = true;
    if (m_heartbeat)   m_heartbeat->stop();
    if (m_connectTimer) m_connectTimer->stop();
    if (m_restartTimer) m_restartTimer->stop();
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(3000);
    }
}

// ---- 경로 해석 -------------------------------------------------
QString ObsProcessManager::resolveExePath() const {
    const QString rel = "/bin/64bit/obs64.exe";

    if (!m_cfg.exePath.isEmpty() && QFileInfo::exists(m_cfg.exePath))
        return m_cfg.exePath;

    const QString deployed =
        QCoreApplication::applicationDirPath() + "/obs" + rel;
    if (QFileInfo::exists(deployed))
        return deployed;

#ifdef UWP_OBS_DEV_ROOT
    const QString devTree = QStringLiteral(UWP_OBS_DEV_ROOT) + rel;
    if (QFileInfo::exists(devTree))
        return devTree;
#endif
    return QString();
}

// ---- 포터블 config 경로 ----------------------------------------
// 포터블 OBS 는 <obsRoot>/config/obs-studio 에 설정/sentinel 을 둔다.
// exe = <obsRoot>/bin/64bit/obs64.exe → 두 단계 상위가 OBS 루트.
QString ObsProcessManager::portableConfigDir() const {
    const QString exe = resolveExePath();
    if (exe.isEmpty()) return QString();
    QDir d(QFileInfo(exe).absolutePath());      // <root>/bin/64bit
    if (!d.cdUp() || !d.cdUp()) return QString();  // → <root>
    const QString cfg = d.absoluteFilePath(QStringLiteral("config/obs-studio"));
    return QDir(cfg).exists() ? cfg : QString();
}

// ---- 기동 전 크래시 상태 정리 ----------------------------------
// OBS 32.x 의 "비정상 종료 → 안전 모드?" 대화상자를 구조적으로 차단한다.
// 안전 모드는 obs-websocket 을 비활성화하므로(=engine=obs 송출 불가) 반드시
// 회피해야 한다. --disable-shutdown-check 플래그가 항상 막아주지는 않으므로
// 근본 원인인 sentinel 파일을 직접 제거한다.
//
// 종료는 의도적으로 kill(TerminateProcess)로 한다 — OBS 32.x 는 정상 종료
// 경로(OBSBasic/Preview 그래픽 컨텍스트 해제)에서 크래시하는 알려진 버그가
// 있어, kill 로 teardown 자체를 건너뛴다. 그 부작용(sentinel 잔존)은 이
// 함수가 다음 기동 때 정리하므로 안전 모드 프롬프트는 뜨지 않는다.
// (씬·소스는 매 기동 시 재시드하므로 OBS 설정 영속에 의존하지 않음.)
void ObsProcessManager::cleanObsCrashState() {
    const QString cfg = portableConfigDir();
    if (cfg.isEmpty()) {
        qInfo() << "ObsProcessManager: portable config dir not found "
                   "— skip crash-state cleanup";
        return;
    }

    // 잔존 sentinel(run_*) 제거.
    //   OBS 는 기동 시 .sentinel/run_<uuid> 를 만들고, 정상 종료 시 자기 것을
    //   삭제한다. 강제 종료된 세션의 파일이 남아 있으면 다음 기동에서
    //   비정상 종료로 오탐 → 안전 모드 프롬프트. 기동 전 모두 지워 차단.
    QDir sentinel(cfg + QStringLiteral("/.sentinel"));
    if (sentinel.exists()) {
        const QStringList runs =
            sentinel.entryList({ QStringLiteral("run_*") }, QDir::Files);
        int removed = 0;
        for (const QString& f : runs)
            if (sentinel.remove(f)) ++removed;
        if (removed > 0)
            qInfo() << "ObsProcessManager: cleared" << removed
                    << "stale OBS sentinel(s) — no safe-mode prompt";
    }
}

// ---- 상태 ------------------------------------------------------
void ObsProcessManager::setState(State s) {
    if (m_state == s) return;
    m_state = s;
    emit stateChanged(s);
}

// ---- 기동 ------------------------------------------------------
void ObsProcessManager::start() {
    if (m_state == State::Failed) return;

    const QString exe = resolveExePath();
    if (exe.isEmpty()) {
        qWarning() << "ObsProcessManager: OBS exe not found "
                      "(set settings.obs.exe_path or place third_party/obs — "
                      "see third_party/obs/README.md)";
        setState(State::Failed);
        emit failed(QStringLiteral("OBS executable not found"));
        return;
    }

    m_stopRequested   = false;
    m_connectElapsedMs = 0;
    m_hbFailStreak    = 0;

    // 기동 전: 이전 세션(강제 종료됨)이 남긴 sentinel 을 제거한다. → OBS 가
    // "비정상 종료 감지 → 안전 모드?" 대화상자를 띄우지 않는다(안전 모드는
    // obs-websocket 을 비활성화하므로 engine=obs 송출 불가).
    cleanObsCrashState();

    if (!m_proc) {
        m_proc = new QProcess(this);
        connect(m_proc, &QProcess::started,
                this, &ObsProcessManager::onProcessStarted);
        connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(
                            &QProcess::finished),
                this, [this](int code, QProcess::ExitStatus) {
                    onProcessFinished(code);
                });
    }

    m_proc->setProgram(exe);
    m_proc->setArguments({ QStringLiteral("--portable"),
                           QStringLiteral("--disable-updater"),
                           QStringLiteral("--disable-shutdown-check") });
    // OBS 는 작업 디렉터리 기준으로 data/ 를 찾으므로 exe 폴더로 고정.
    m_proc->setWorkingDirectory(QFileInfo(exe).absolutePath());

    qInfo() << "ObsProcessManager: launching" << exe;
    setState(State::Launching);
    m_proc->start();
}

void ObsProcessManager::onProcessStarted() {
    m_pid = m_proc->processId();
    qInfo() << "ObsProcessManager: OBS started, pid=" << m_pid
            << "— awaiting obs-websocket";
    setState(State::WaitingForWebSocket);

    if (!m_client) {
        m_client = new ObsClient(this);
        connect(m_client, &ObsClient::ready,
                this, &ObsProcessManager::onClientReady);
        connect(m_client, &ObsClient::closed,
                this, &ObsProcessManager::onClientClosed);
        connect(m_client, &ObsClient::socketError, this,
                [this](const QString&) { onClientClosed(); });
    }
    if (!m_connectTimer) {
        m_connectTimer = new QTimer(this);
        m_connectTimer->setSingleShot(true);
        connect(m_connectTimer, &QTimer::timeout,
                this, &ObsProcessManager::tryConnect);
    }
    tryConnect();
}

// 폴링: 실패 시그널(onClientClosed) 또는 최초 호출에서만 진입.
void ObsProcessManager::tryConnect() {
    if (m_stopRequested || m_state == State::Ready) return;
    if (m_connectElapsedMs >= kConnectTimeoutMs) {
        qWarning() << "ObsProcessManager: obs-websocket not ready within"
                   << kConnectTimeoutMs << "ms — treating as launch failure";
        if (m_proc && m_proc->state() != QProcess::NotRunning)
            m_proc->kill();   // → onProcessFinished → 재기동 경로
        return;
    }
    m_client->connectToObs(m_cfg.wsUrl, m_cfg.wsPassword);
}

void ObsProcessManager::onClientClosed() {
    if (m_stopRequested || m_state != State::WaitingForWebSocket) return;
    // 아직 OBS 가 obs-websocket 서버를 안 열었을 뿐 → 간격 후 재시도.
    m_connectElapsedMs += kConnectIntervalMs;
    m_connectTimer->start(kConnectIntervalMs);
}

void ObsProcessManager::onClientReady() {
    qInfo() << "ObsProcessManager: obs-websocket Ready";
    m_connectTimer->stop();
    m_restartCount = 0;
    m_hbFailStreak = 0;
    setState(State::Ready);

    hideObsMainWindow();
    openProgramProjector();

    if (!m_heartbeat) {
        m_heartbeat = new QTimer(this);
        connect(m_heartbeat, &QTimer::timeout,
                this, &ObsProcessManager::onHeartbeat);
    }
    m_heartbeat->start(kHeartbeatMs);
    emit ready();
}

// ---- 하트비트 --------------------------------------------------
void ObsProcessManager::onHeartbeat() {
    if (m_state != State::Ready || !m_client) return;
    m_client->request(QStringLiteral("GetVersion"), {},
        [this](bool ok, const QJsonObject&, const QString&) {
            if (m_state != State::Ready) return;
            if (ok) {
                m_hbFailStreak = 0;
            } else if (++m_hbFailStreak >= kHbFailLimit) {
                qWarning() << "ObsProcessManager: heartbeat failed"
                           << m_hbFailStreak << "times — OBS hung, restarting";
                if (m_proc && m_proc->state() != QProcess::NotRunning)
                    m_proc->kill();   // → onProcessFinished → 재기동
            }
        });
}

// ---- 종료 / 재기동 ---------------------------------------------
void ObsProcessManager::onProcessFinished(int exitCode) {
    if (m_heartbeat) m_heartbeat->stop();
    if (m_connectTimer) m_connectTimer->stop();

    if (m_stopRequested) {
        qInfo() << "ObsProcessManager: OBS stopped (requested)";
        setState(State::Stopped);
        return;
    }
    qWarning() << "ObsProcessManager: OBS exited unexpectedly (code="
               << exitCode << ") — scheduling restart";
    scheduleRestart();
}

void ObsProcessManager::scheduleRestart() {
    if (++m_restartCount > kMaxRestarts) {
        qCritical() << "ObsProcessManager: restart limit exceeded — giving up";
        setState(State::Failed);
        emit failed(QStringLiteral("OBS restart limit exceeded"));
        return;
    }
    const int exp   = qMin(m_restartCount - 1, 4);
    const int delay = qMin(1000 * (1 << exp), 15000);  // 1s..15s
    qInfo() << "ObsProcessManager: restart" << m_restartCount
            << "in" << delay << "ms";
    setState(State::Restarting);

    if (!m_restartTimer) {
        m_restartTimer = new QTimer(this);
        m_restartTimer->setSingleShot(true);
        connect(m_restartTimer, &QTimer::timeout, this, [this]() { start(); });
    }
    m_restartTimer->start(delay);
}

void ObsProcessManager::stop() {
    m_stopRequested = true;
    if (m_heartbeat)    m_heartbeat->stop();
    if (m_connectTimer) m_connectTimer->stop();
    if (m_restartTimer) m_restartTimer->stop();
    setState(State::Stopping);
    if (m_client) m_client->disconnectFromObs();
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();   // teardown 크래시 회피 — ~ObsProcessManager 와 동일 이유
        m_proc->waitForFinished(3000);
    } else {
        setState(State::Stopped);
    }
}

// ---- Win32: 메인창 숨김 ----------------------------------------
// Ready 직후, 프로젝터 오픈 전에 호출 → 그 시점엔 메인창만 존재하므로
// pid 의 top-level 가시 창을 숨겨도 프로젝터와 충돌하지 않는다.
#ifdef _WIN32
namespace {
struct HideCtx { DWORD pid; int hidden; HWND firstHidden; };
BOOL CALLBACK hideEnumProc(HWND h, LPARAM lp) {
    auto* c = reinterpret_cast<HideCtx*>(lp);
    DWORD wpid = 0;
    GetWindowThreadProcessId(h, &wpid);
    if (wpid == c->pid && IsWindowVisible(h)
        && GetWindow(h, GW_OWNER) == nullptr) {
        ShowWindow(h, SW_HIDE);
        if (!c->firstHidden) c->firstHidden = h;   // 메인창 HWND 기억
        ++c->hidden;
    }
    return TRUE;
}

// 메인창(skip)을 제외한 가시 top-level 창 = 프로젝터 → WM_CLOSE.
// 프로젝터 창을 닫아도 OBS 본체는 계속 실행된다(메인창만 앱 종료 트리거).
// 주의: OBS 풀스크린 프로젝터는 메인창이 소유(owner)하는 top-level 창이라
//       GW_OWNER==null 필터로는 잡히지 않는다 → owner 필터를 두지 않는다.
//       (EnumWindows 는 top-level 창만 열거하므로 자식 위젯은 대상이 아님)
struct CloseProjCtx { DWORD pid; HWND skip; int closed; };
BOOL CALLBACK closeProjEnumProc(HWND h, LPARAM lp) {
    auto* c = reinterpret_cast<CloseProjCtx*>(lp);
    DWORD wpid = 0;
    GetWindowThreadProcessId(h, &wpid);
    if (wpid == c->pid && h != c->skip && IsWindowVisible(h)) {
        PostMessageW(h, WM_CLOSE, 0, 0);
        ++c->closed;
    }
    return TRUE;
}
} // namespace
#endif

void ObsProcessManager::hideObsMainWindow() {
    // 디버그: UWP_OBS_NOHIDE=1 이면 OBS 메인창을 숨기지 않음
    // (소스/씬/재생 상태를 OBS UI 에서 직접 확인용).
    if (qEnvironmentVariableIntValue("UWP_OBS_NOHIDE") > 0) {
        qInfo() << "ObsProcessManager: UWP_OBS_NOHIDE — OBS 창 숨김 생략";
        return;
    }
#ifdef _WIN32
    // 창이 아직 안 떴을 수 있어 몇 번 재시도.
    static constexpr int kMaxTries = 6;
    auto attempt = std::make_shared<int>(0);
    auto run = std::make_shared<std::function<void()>>();
    *run = [this, attempt, run]() {
        if (m_state != State::Ready) return;
        HideCtx ctx{ static_cast<DWORD>(m_pid), 0, nullptr };
        EnumWindows(hideEnumProc, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.hidden > 0) {
            m_mainHwnd = ctx.firstHidden;   // 프로젝터 식별 시 제외할 메인창
            qInfo() << "ObsProcessManager: hid" << ctx.hidden
                    << "OBS main window(s)";
            return;
        }
        if (++(*attempt) < kMaxTries)
            QTimer::singleShot(400, this, [run]() { (*run)(); });
        else
            qWarning() << "ObsProcessManager: OBS main window not found "
                          "to hide (may be visible)";
    };
    (*run)();
#else
    qInfo() << "ObsProcessManager: window hide is Windows-only — skipped";
#endif
}

// ---- 프로젝터 (best-effort) ------------------------------------
void ObsProcessManager::openProgramProjector() {
    if (!m_client) return;
    QJsonObject d;
    d[QStringLiteral("videoMixType")] =
        QStringLiteral("OBS_WEBSOCKET_VIDEO_MIX_TYPE_PROGRAM");
    d[QStringLiteral("monitorIndex")] = m_cfg.projectorMonitor;
    m_client->request(QStringLiteral("OpenVideoMixProjector"), d,
        [](bool ok, const QJsonObject&, const QString& c) {
            if (ok)
                qInfo() << "ObsProcessManager: Program projector opened";
            else
                qWarning() << "ObsProcessManager: OpenVideoMixProjector "
                              "failed (obs-websocket 버전 의존) —" << c;
        });
}

// ---- 프로젝터 창 닫기 (Win32) ----------------------------------
// obs-websocket v5 에는 프로젝터를 닫는 요청이 없으므로 Win32 로 처리.
// 숨긴 메인창(m_mainHwnd)은 제외 → 본체는 살아있고 프로젝터만 닫힌다.
void ObsProcessManager::closeProjectorWindows() {
#ifdef _WIN32
    if (!m_mainHwnd) {
        // 메인창을 식별 못 한 상태(UWP_OBS_NOHIDE 등)에서는 가시 창을 무차별
        // 닫으면 본체까지 종료될 위험 → 닫지 않는다(새 프로젝터가 추가될 뿐).
        qInfo() << "ObsProcessManager: main window unknown — skip projector close";
        return;
    }
    CloseProjCtx ctx{ static_cast<DWORD>(m_pid),
                      reinterpret_cast<HWND>(m_mainHwnd), 0 };
    EnumWindows(closeProjEnumProc, reinterpret_cast<LPARAM>(&ctx));
    qInfo() << "ObsProcessManager: closed" << ctx.closed
            << "projector window(s) (excluding main)";
#endif
}

// ---- 프로젝터 모니터 변경 --------------------------------------
void ObsProcessManager::setProjectorMonitor(int monitorIndex) {
    m_cfg.projectorMonitor = monitorIndex;
    qInfo() << "ObsProcessManager: projector monitor →" << monitorIndex;
    if (m_state != State::Ready || !m_client)
        return;   // 다음 Ready 에서 새 인덱스로 열림
    // 기존 프로젝터를 닫고, 닫힘이 처리될 약간의 여유 후 새 모니터에 재오픈.
    closeProjectorWindows();
    QTimer::singleShot(200, this, [this]() { openProgramProjector(); });
}

} // namespace uwp
