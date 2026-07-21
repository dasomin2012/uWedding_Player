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
#include <tlhelp32.h>
#endif

namespace uwp {

ObsProcessManager::ObsProcessManager(const ObsConfig& cfg, QObject* parent)
    : QObject(parent), m_cfg(cfg) {
#ifdef _WIN32
    // Job Object 를 미리 만든다. KILL_ON_JOB_CLOSE 로 우리 프로세스 exit/crash
    // 시 OS 가 job 내 모든 자식(= 여기 assign 될 OBS) 를 자동 종료.
    //   * BreakawayOK = false  → OBS 가 CREATE_BREAKAWAY_FROM_JOB 으로 벗어나기 방지
    //   * 실패해도 치명적 아님 — 종료 시 우리 destructor 의 kill() 이 폴백.
    HANDLE h = CreateJobObjectW(nullptr, nullptr);
    if (h) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION eli{};
        eli.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(h, JobObjectExtendedLimitInformation,
                                     &eli, sizeof(eli))) {
            qWarning() << "ObsProcessManager: SetInformationJobObject failed"
                       << "(GetLastError=" << GetLastError() << ")";
            CloseHandle(h);
            h = nullptr;
        }
    } else {
        qWarning() << "ObsProcessManager: CreateJobObjectW failed"
                   << "(GetLastError=" << GetLastError() << ")";
    }
    m_jobHandle = h;
#endif
}

ObsProcessManager::~ObsProcessManager() {
    // 앱 종료 시 OBS 자식 프로세스를 남기지 않는다.
    // kill() 직행: OBS 32.x 는 정상 종료 teardown 에서 크래시하는 버그가 있어
    // WM_CLOSE(terminate) 로 깨끗이 닫게 두면 "OBS has crashed" 창이 뜬다.
    // TerminateProcess 로 teardown 자체를 건너뛴다(상세는 cleanObsCrashState()).
    m_stopRequested = true;
    if (m_heartbeat)   m_heartbeat->stop();
    if (m_connectTimer) m_connectTimer->stop();
    if (m_restartTimer) m_restartTimer->stop();
    // stop() 없이 소멸자만 실행되는 경로에서도 클라이언트 콜백이 kill 도중
    // 발화해 wrong-thread 로 QTimer::stop 을 부르지 않도록 명시 disconnect.
    if (m_client) m_client->disconnectFromObs();
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(3000);
    }
#ifdef _WIN32
    // Job 핸들 닫기 — 여기까지 도달했으면 이미 OBS 는 위에서 정리됨.
    // 우리 프로세스가 크래시로 여기 도달 못 하면, OS 가 핸들을 회수하면서
    // KILL_ON_JOB_CLOSE 로 OBS 를 자동 종료(이 매커니즘의 핵심).
    if (m_jobHandle) {
        CloseHandle(reinterpret_cast<HANDLE>(m_jobHandle));
        m_jobHandle = nullptr;
    }
#endif
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

    // 우리 세션과 무관한 obs64.exe(이전 세션 orphan 등)를 제거 → OBS 자체
    // "이미 실행 중" 다이얼로그 예방 + 포트 4455 점유 충돌 방지.
    // 우리 pid 는 아직 없으므로 무조건 전량 스윕(기동 전이라 자기충돌 없음).
    sweepStrayObsProcesses();

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
    // --multi: 스윕이 실패했거나 우리가 놓친 인스턴스가 있어도 OBS 의
    //          "이미 실행 중" 다이얼로그를 원천 억제. 벨트+서스펜더.
    m_proc->setArguments({ QStringLiteral("--portable"),
                           QStringLiteral("--multi"),
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
    // Job Object 부착 — 우리 프로세스가 어떤 이유로 죽어도 이 OBS 는 OS 가 종료.
    assignProcessToJob();
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

// ---- 시작 시 stray obs64.exe 스윕 (Windows) --------------------
// 우리 프로세스 crash / 이전 세션 orphan / 사용자가 수동 실행한 잔재를 모두
// 제거. Job Object 가 부모-in-job 환경에서 KILL_ON_JOB_CLOSE 로 청소하지
// 못하는 케이스의 실질적 방어선. 우리 pid 는 아직 없으므로(m_pid==0) 전량 kill.
// 재기동(restart) 시엔 m_pid 가 이전 실행 값이지만 그 프로세스는 이미 죽어
// snapshot 에 없다 → 안전.
void ObsProcessManager::sweepStrayObsProcesses() {
#ifdef _WIN32
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        qWarning() << "ObsProcessManager: sweep snapshot failed "
                      "(GetLastError=" << GetLastError() << ")";
        return;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    int killed = 0, seen = 0;
    if (Process32FirstW(snap, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"obs64.exe") != 0) continue;
            ++seen;
            // 이전 세션의 pid 와 같아도 이미 죽었을 것이라 자기충돌 없음.
            HANDLE h = OpenProcess(
                PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
            if (!h) continue;
            if (TerminateProcess(h, 1)) {
                WaitForSingleObject(h, 2000);   // 확실히 죽을 때까지
                ++killed;
            }
            CloseHandle(h);
        } while (Process32NextW(snap, &entry));
    }
    CloseHandle(snap);
    if (seen > 0)
        qInfo() << "ObsProcessManager: swept" << killed
                << "/" << seen << "stray obs64.exe process(es)";
#endif
}

// ---- Job Object 부착 (Windows) ---------------------------------
// OBS 가 이 job 에 속하면 우리 프로세스가 죽는 순간(정상/크래시/작업관리자 킬)
// 커널이 JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE 로 자동 청소한다. destructor
// 실행 여부에 의존하지 않는 것이 핵심 — 크래시 시 OBS 고아 근원 차단.
void ObsProcessManager::assignProcessToJob() {
#ifdef _WIN32
    if (!m_jobHandle || m_pid == 0) return;
    // 프로세스 종료·assign 만 필요하므로 최소 권한.
    HANDLE hProc = OpenProcess(
        PROCESS_TERMINATE | PROCESS_SET_QUOTA, FALSE,
        static_cast<DWORD>(m_pid));
    if (!hProc) {
        qWarning() << "ObsProcessManager: OpenProcess pid=" << m_pid
                   << "failed (GetLastError=" << GetLastError() << ")";
        return;
    }
    if (!AssignProcessToJobObject(
            reinterpret_cast<HANDLE>(m_jobHandle), hProc)) {
        const DWORD err = GetLastError();
        qWarning() << "ObsProcessManager: AssignProcessToJobObject failed"
                   << "(GetLastError=" << err << ") — OBS may orphan on crash";
        // 진단: 우리 프로세스가 이미 다른 job 에 소속돼 nested 가 막힌 상황인지.
        BOOL parentInJob = FALSE;
        if (IsProcessInJob(GetCurrentProcess(), nullptr, &parentInJob))
            qWarning() << "  parent(uWeddingPlayer) inJob=" << bool(parentInJob);
    } else {
        // 부착 성공 확인 — IsProcessInJob(pid, ourJob) 로 실제 소속 검증.
        BOOL inOurJob = FALSE;
        if (IsProcessInJob(hProc, reinterpret_cast<HANDLE>(m_jobHandle),
                           &inOurJob) && inOurJob) {
            qInfo() << "ObsProcessManager: OBS assigned to job "
                       "(auto-kill on parent exit, verified inJob=true)";
        } else {
            qWarning() << "ObsProcessManager: AssignProcessToJobObject "
                          "returned success but IsProcessInJob=false "
                          "— job kill may not work";
        }
        // 우리 프로세스가 이미 다른 job 에 있는지 로그 (Windows 8+ 는 nested 지원
        // 이지만 특정 조건에서 KILL_ON_JOB_CLOSE 가 상위 job 에 흡수되는 케이스 존재).
        BOOL parentInJob = FALSE;
        if (IsProcessInJob(GetCurrentProcess(), nullptr, &parentInJob))
            qInfo() << "  parent(uWeddingPlayer) inJob=" << bool(parentInJob);
    }
    CloseHandle(hProc);
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
