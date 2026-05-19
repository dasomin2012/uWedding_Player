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
    m_stopRequested = true;
    if (m_heartbeat)   m_heartbeat->stop();
    if (m_connectTimer) m_connectTimer->stop();
    if (m_restartTimer) m_restartTimer->stop();
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->terminate();
        if (!m_proc->waitForFinished(3000))
            m_proc->kill();
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
        m_proc->terminate();
        if (!m_proc->waitForFinished(3000))
            m_proc->kill();
    } else {
        setState(State::Stopped);
    }
}

// ---- Win32: 메인창 숨김 ----------------------------------------
// Ready 직후, 프로젝터 오픈 전에 호출 → 그 시점엔 메인창만 존재하므로
// pid 의 top-level 가시 창을 숨겨도 프로젝터와 충돌하지 않는다.
#ifdef _WIN32
namespace {
struct HideCtx { DWORD pid; int hidden; };
BOOL CALLBACK hideEnumProc(HWND h, LPARAM lp) {
    auto* c = reinterpret_cast<HideCtx*>(lp);
    DWORD wpid = 0;
    GetWindowThreadProcessId(h, &wpid);
    if (wpid == c->pid && IsWindowVisible(h)
        && GetWindow(h, GW_OWNER) == nullptr) {
        ShowWindow(h, SW_HIDE);
        ++c->hidden;
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
        HideCtx ctx{ static_cast<DWORD>(m_pid), 0 };
        EnumWindows(hideEnumProc, reinterpret_cast<LPARAM>(&ctx));
        if (ctx.hidden > 0) {
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

} // namespace uwp
