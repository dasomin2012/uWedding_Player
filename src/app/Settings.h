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

    // ----- Phase 1 전용: 테스트 영상 -----
    QString testVideoPath() const { return m_testVideoPath; }
    void    setTestVideoPath(const QString& p) { m_testVideoPath = p; }

private:
    QString m_version             = "1.0.0";
    int     m_canvasWidth         = 10368;
    int     m_canvasHeight        = 2808;
    int     m_outputMonitorIndex  = 1;
    QString m_outputRenderMode    = "fit";   // fit | fill | stretch
    QString m_takeDefaultMode     = "fade";  // cut | fade
    int     m_takeFadeDurationMs  = 800;
    NovaStarConfig m_novaStar;
    QString m_testVideoPath       = "data/sample.mp4";  // 비어있으면 Live 검은 화면 유지
};

} // namespace uwp
