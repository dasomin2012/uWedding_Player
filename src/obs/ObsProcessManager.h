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

    // 기동 시 OBS 프로파일 INI 에 반영할 캔버스 크기. obs-websocket 의
    // SetVideoSettings 는 4096 상한 하드코딩 — libobs 코어 자체는 16384
    // 까지 지원하므로 프로파일 basic.ini 를 사전에 기록해 우회한다.
    // Application 이 start() 전에 settings.canvas 값으로 세팅.
    void setInitialCanvas(int w, int h) { m_initCanvasW = w; m_initCanvasH = h; }

    // 풀스크린 프로젝터를 다른 모니터로 옮긴다(engine=obs 출력 모니터 변경).
    // Ready 상태면 기존 프로젝터 창을 닫고 새 모니터에 다시 연다. 아니면
    // 인덱스만 저장 → 다음 Ready 에서 새 모니터로 열린다. (OBS 재기동 없음)
    void setProjectorMonitor(int monitorIndex);
    // 스크린 모드: 데스크톱 가상 좌표계 위 임의 사각형에 windowed projector.
    // OBS 는 monitorIndex=-1 + projectorGeometry="WxH+X+Y" 로 이를 지원.
    // Ready 면 즉시 재오픈, 아니면 값만 저장 → 다음 Ready 때 사용.
    void setProjectorGeometry(int x, int y, int w, int h);

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
    void  sweepStrayObsProcesses();     // 기동 전: 우리와 무관한 obs64.exe 강제 종료
    void  writeCanvasToProfileIni();    // 기동 전: 4096 상한 우회용 basic.ini [Video] 사전 기록
    void  scheduleRestart();
    void  hideObsMainWindow();          // Win32 (#ifdef _WIN32)
    void  closeProjectorWindows();      // Win32: 메인창 제외 가시 top-level 창 닫기
    void  openProgramProjector();       // best-effort — monitor 또는 geometry 모드
    void  makeProjectorBorderless();    // 스크린 모드용: WS_POPUP 로 테두리 제거 + 정확 배치

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

    // 스크린 모드 지원 — true 면 openProgramProjector 가 monitorIndex=-1 +
    // projectorGeometry="WxH+X+Y" 로 windowed projector 를 연다.
    bool m_useGeometry = false;
    int  m_geoX = 0, m_geoY = 0, m_geoW = 1920, m_geoH = 1080;

    // 기동 시 OBS 프로파일 basic.ini 에 사전 기록할 초기 캔버스. Application 이
    // setInitialCanvas() 로 주입. 0 이면 INI 를 건드리지 않음(기존 값 유지).
    int  m_initCanvasW = 0;
    int  m_initCanvasH = 0;

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
