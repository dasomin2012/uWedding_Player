#pragma once

#include <QString>
#include <QStringList>
#include <QSize>

namespace uwp {

struct NovaStarConfig {
    bool    enabled         = false;
    QString host            = "192.168.1.100";
    int     port            = 5200;
    QString protocol        = "udp";
    QString defaultPresetId = "";   // O5 임시 매핑. Phase 5 ProgramRepository 도입 시 제거.
};

// 송출 백엔드가 OBS 일 때만 사용. obs-websocket(네트워크 프로토콜)으로만
// 제어 — libobs 를 링크하지 않으므로 GPL 전염 없음.
struct ObsConfig {
    QString exePath;                          // 포터블 obs64.exe (비어있으면 third_party/obs 자동 탐지)
    QString wsUrl            = "ws://127.0.0.1:4455";
    QString wsPassword;                       // obs-websocket 인증 (없으면 빈 문자열)
    QString sceneA           = "UWP_PGM_A";   // Studio Mode 핑퐁 씬 A
    QString sceneB           = "UWP_PGM_B";   // Studio Mode 핑퐁 씬 B
    int     projectorMonitor = 1;             // 풀스크린 프로젝터 모니터 인덱스
};

// Phase 1: data/settings.json 파일 I/O.
// 스키마는 CLAUDE.md의 setup.json 형식을 따른다.
class Settings {
public:
    bool load(const QString& path);
    bool save(const QString& path) const;
    bool save() const;                 // 마지막 load/save 경로에 저장

    // ----- Canvas -----
    int   canvasWidth()  const { return m_canvasWidth; }
    int   canvasHeight() const { return m_canvasHeight; }
    QSize canvasSize()   const { return { m_canvasWidth, m_canvasHeight }; }
    void  setCanvasSize(int w, int h);

    // ----- Output -----
    int     outputMonitorIndex() const { return m_outputMonitorIndex; }
    void    setOutputMonitorIndex(int idx);
    QString outputRenderMode()   const { return m_outputRenderMode; }
    // 출력 모드: "monitor"(모니터 전체) | "screen"(좌표+크기 지정) — 웨딩홀
    // LED 스크린이 데스크톱 어느 좌표에 걸쳐 있어도 정확한 영역에 송출.
    QString outputMode()         const { return m_outputMode; }
    void    setOutputMode(const QString& m) { m_outputMode = m; }
    int     outputX() const { return m_outputX; }
    int     outputY() const { return m_outputY; }
    void    setOutputOrigin(int x, int y) { m_outputX = x; m_outputY = y; }

    // ----- Take -----
    QString takeDefaultMode()    const { return m_takeDefaultMode; }
    void    setTakeDefaultMode(const QString& m) { m_takeDefaultMode = m; }
    int     takeFadeDurationMs() const { return m_takeFadeDurationMs; }

    // ----- NovaStar -----
    const NovaStarConfig& novaStar() const { return m_novaStar; }

    // ----- 송출 백엔드 (O1+) -----
    // "qt"  = LiveWindow/libVLC (기본, 동작 무변경)
    // "obs" = ObsLiveBackend (obs-websocket, O4+ 에서 활성)
    QString             engine() const { return m_engine; }
    void                setEngine(const QString& e) { m_engine = e; }
    const ObsConfig&    obs()    const { return m_obs; }
    void                setObsProjectorMonitor(int idx) { m_obs.projectorMonitor = idx; }

    // ----- Snapshot / FFmpeg (Phase 2) -----
    // 비어있으면 third_party/ffmpeg → PATH 순으로 자동 탐지.
    QString ffmpegPath()       const { return m_ffmpegPath; }
    void    setFfmpegPath(const QString& p) { m_ffmpegPath = p; }
    // 실행파일 기준 상대경로 가능. 스냅샷 png 캐시 디렉터리.
    QString snapshotCacheDir() const { return m_snapshotCacheDir; }

    // ----- Editor (Phase 3) -----
    QString mediaDir()      const { return m_mediaDir; }       // 마지막 폴더
    void    setMediaDir(const QString& d) { m_mediaDir = d; }
    QString sceneScratch()  const { return m_sceneScratch; }   // 작업 씬 파일

    // Media Files 패널에 기억할 소스(파일 또는 폴더 경로) 목록.
    // 다음 실행 시 복원 — 폴더는 재스캔, 파일은 그대로 추가.
    QStringList mediaSources() const { return m_mediaSources; }
    void        setMediaSources(const QStringList& s) { m_mediaSources = s; }

    // ----- Phase 1 전용: 테스트 영상 -----
    QString testVideoPath() const { return m_testVideoPath; }
    void    setTestVideoPath(const QString& p) { m_testVideoPath = p; }

private:
    QString m_version             = "1.0.0";
    int     m_canvasWidth         = 1920;
    int     m_canvasHeight        = 1080;
    int     m_outputMonitorIndex  = 1;
    QString m_outputRenderMode    = "fit";   // fit | fill | stretch
    QString m_outputMode          = "monitor"; // monitor | screen
    int     m_outputX             = 0;
    int     m_outputY             = 0;
    QString m_takeDefaultMode     = "fade";  // cut | fade
    int     m_takeFadeDurationMs  = 800;
    NovaStarConfig m_novaStar;
    QString        m_engine       = "qt";    // qt | obs
    ObsConfig      m_obs;
    QString m_testVideoPath       = "data/sample.mp4";  // 비어있으면 Live 검은 화면 유지
    QString m_ffmpegPath;                               // 비어있으면 자동 탐지
    QString m_snapshotCacheDir    = "data/cache";
    QString m_mediaDir;                                 // 마지막 미디어 폴더
    QString m_sceneScratch        = "data/scene.json";
    QStringList m_mediaSources;                         // Media Files 패널 기억 소스
    mutable QString m_loadedPath;                       // 마지막 load/save 경로
};

} // namespace uwp
