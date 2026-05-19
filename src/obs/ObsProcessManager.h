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
    void  scheduleRestart();
    void  hideObsMainWindow();          // Win32 (#ifdef _WIN32)
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

    static constexpr int kConnectIntervalMs = 500;
    static constexpr int kConnectTimeoutMs  = 25000;  // priming 포함 넉넉히
    static constexpr int kHeartbeatMs       = 5000;
    static constexpr int kHbFailLimit       = 3;      // 연속 실패 시 hung 판정
    static constexpr int kMaxRestarts       = 6;      // 초과 시 Failed
};

} // namespace uwp
