#pragma once

#include <QObject>
#include <QString>

#include "app/Settings.h"   // ObsConfig (값 보관)

class QProcess;
class QTimer;

namespace uwp {

class ObsClient;

// 포터블 OBS 프로세스의 수명을 관리한다.
//
// 책임: exe 경로 해석 → QProcess 기동 → obs-websocket Ready 대기(폴링)
//      → 메인창 숨김(Win32) → Program 프로젝터 오픈(best-effort)
//      → 하트비트 감시 → 크래시 시 백오프 자동 재기동.
//
// libobs 미접촉. 제어는 내부 ObsClient(obs-websocket)로만 — GPL 격리 유지.
// Take 백엔드 교체는 O4 의 몫이며 여기서는 다루지 않는다.
class ObsProcessManager : public QObject {
    Q_OBJECT
public:
    enum class State {
        Idle,                 // 시작 전
        Launching,            // QProcess 기동 중
        WaitingForWebSocket,  // 프로세스 떴고 obs-websocket Identify 대기
        Ready,                // Identified — 제어 가능
        Restarting,           // 비정상 종료 → 백오프 후 재기동 예정
        Stopping,             // 정상 종료 진행 중
        Stopped,              // 정상 종료 완료
        Failed                // 복구 불가(경로 없음 / 재기동 한계 초과)
    };
    Q_ENUM(State)

    explicit ObsProcessManager(const ObsConfig& cfg, QObject* parent = nullptr);
    ~ObsProcessManager() override;

    void start();
    void stop();   // 정상 종료 (자동 재기동 안 함)

    // 풀스크린 프로젝터를 다른 모니터로 옮긴다(engine=obs 출력 모니터 변경).
    // Ready 상태면 기존 프로젝터 창을 닫고 새 모니터에 다시 연다. 아니면
    // 인덱스만 저장 → 다음 Ready 에서 새 모니터로 열린다. (OBS 재기동 없음)
    void setProjectorMonitor(int monitorIndex);

    State      state()  const { return m_state; }
    ObsClient* client() const { return m_client; }  // O4 가 동일 연결 재사용

signals:
    void stateChanged(State s);
    void ready();                       // Ready 진입
    void failed(const QString& reason); // Failed 진입

private slots:
    void onProcessStarted();
    void onProcessFinished(int exitCode);
    void onClientReady();
    void onClientClosed();
    void tryConnect();      // 폴링: ObsClient 재연결 시도
    void onHeartbeat();

private:
    void  setState(State s);
    QString resolveExePath() const;     // "" = 못 찾음
    QString portableConfigDir() const;  // <obsRoot>/config/obs-studio ("" = 못 찾음)
    void  cleanObsCrashState();         // 기동 전: 잔존 sentinel 제거 + ConfirmOnExit 해제
    void  scheduleRestart();
    void  hideObsMainWindow();          // Win32 (#ifdef _WIN32)
    void  closeProjectorWindows();      // Win32: 메인창 제외 가시 top-level 창 닫기
    void  openProgramProjector();       // best-effort

    ObsConfig  m_cfg;            // 값 복사 (Settings 수명과 분리)
    QProcess*  m_proc        = nullptr;
    ObsClient* m_client      = nullptr;
    QTimer*    m_connectTimer = nullptr;  // Ready 까지 폴링
    QTimer*    m_heartbeat   = nullptr;
    QTimer*    m_restartTimer = nullptr;

    State m_state          = State::Idle;
    bool  m_stopRequested  = false;       // 정상 종료 의도
    int   m_connectElapsedMs = 0;
    int   m_restartCount   = 0;
    int   m_hbFailStreak   = 0;
    qint64 m_pid           = 0;
    void* m_mainHwnd       = nullptr;     // 숨긴 OBS 메인창 HWND (프로젝터 식별용)

    // Windows Job Object: KILL_ON_JOB_CLOSE 로 우리 프로세스 종료 시(정상/
    // 크래시/작업관리자 강제 종료 모두 포함) OBS 를 OS 가 자동 정리.
    // HANDLE 을 void* 로 보관해 헤더에서 <windows.h> 노출 회피.
    void* m_jobHandle      = nullptr;
    void assignProcessToJob();            // #ifdef _WIN32 구현. 실패 시 로그만.

    static constexpr int kConnectIntervalMs = 500;
    static constexpr int kConnectTimeoutMs  = 25000;  // priming 포함 넉넉히
    static constexpr int kHeartbeatMs       = 5000;
    static constexpr int kHbFailLimit       = 3;      // 연속 실패 시 hung 판정
    static constexpr int kMaxRestarts       = 6;      // 초과 시 Failed
};

} // namespace uwp
