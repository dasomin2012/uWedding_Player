#pragma once

#include <QWidget>

namespace uwp {

class VideoWidget;

// 확장 모니터로 송출되는 Live 윈도우.
// frameless + fullscreen 기본, 보조 모니터가 없으면 dev 모드로 윈도우 표시.
class LiveWindow : public QWidget {
    Q_OBJECT
public:
    explicit LiveWindow(QWidget* parent = nullptr);
    ~LiveWindow() override;

    void setCanvasSize(int width, int height);

    // monitorIndex 가 유효하면 해당 스크린에 fullscreen 표시.
    // 보조 모니터가 없으면 dev 모드(작은 윈도우) 로 표시.
    void showOnMonitor(int monitorIndex);

    bool playVideo(const QString& path);
    void stopVideo();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    VideoWidget* m_videoWidget   = nullptr;
    int          m_canvasWidth   = 1920;
    int          m_canvasHeight  = 1080;
    bool         m_devMode       = false;
};

} // namespace uwp
