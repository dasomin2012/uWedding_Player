#pragma once

#include <QString>
#include <QSize>

namespace uwp {

struct NovaStarConfig {
    bool    enabled  = false;
    QString host     = "192.168.1.100";
    int     port     = 5200;
    QString protocol = "udp";
};

// Phase 1: data/settings.json 파일 I/O.
// 스키마는 CLAUDE.md의 setup.json 형식을 따른다.
class Settings {
public:
    bool load(const QString& path);
    bool save(const QString& path) const;

    // ----- Canvas -----
    int   canvasWidth()  const { return m_canvasWidth; }
    int   canvasHeight() const { return m_canvasHeight; }
    QSize canvasSize()   const { return { m_canvasWidth, m_canvasHeight }; }
    void  setCanvasSize(int w, int h);

    // ----- Output -----
    int     outputMonitorIndex() const { return m_outputMonitorIndex; }
    void    setOutputMonitorIndex(int idx);
    QString outputRenderMode()   const { return m_outputRenderMode; }

    // ----- Take -----
    QString takeDefaultMode()    const { return m_takeDefaultMode; }
    int     takeFadeDurationMs() const { return m_takeFadeDurationMs; }

    // ----- NovaStar -----
    const NovaStarConfig& novaStar() const { return m_novaStar; }

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

    // ----- Phase 1 전용: 테스트 영상 -----
    QString testVideoPath() const { return m_testVideoPath; }
    void    setTestVideoPath(const QString& p) { m_testVideoPath = p; }

private:
    QString m_version             = "1.0.0";
    int     m_canvasWidth         = 1920;
    int     m_canvasHeight        = 1080;
    int     m_outputMonitorIndex  = 1;
    QString m_outputRenderMode    = "fit";   // fit | fill | stretch
    QString m_takeDefaultMode     = "fade";  // cut | fade
    int     m_takeFadeDurationMs  = 800;
    NovaStarConfig m_novaStar;
    QString m_testVideoPath       = "data/sample.mp4";  // 비어있으면 Live 검은 화면 유지
    QString m_ffmpegPath;                               // 비어있으면 자동 탐지
    QString m_snapshotCacheDir    = "data/cache";
    QString m_mediaDir;                                 // 마지막 미디어 폴더
    QString m_sceneScratch        = "data/scene.json";
};

} // namespace uwp
