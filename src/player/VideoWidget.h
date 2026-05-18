#pragma once

#include <QWidget>
#include <QString>

namespace uwp {

class LivePlayerPool;
class LivePlayer;

// 출력용 네이티브 윈도우(HWND)를 제공하는 위젯.
// 자체 libVLC 인스턴스를 소유하지 않고, 생성 시 LivePlayerPool 에서
// LivePlayer 를 1개 대여하여 재생을 위임한다. 소멸 시 풀로 반환.
// 풀이 stub/SDK 미탑재면 재생은 항상 false (검은 배경 유지).
class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(LivePlayerPool* pool, QWidget* parent = nullptr);
    ~VideoWidget() override;

    bool play(const QString& path);
    void stop();
    void pause();              // 토글
    bool isPlaying() const;

private:
    void bindSurface();        // winId() 를 LivePlayer 에 바인딩

    LivePlayerPool* m_pool   = nullptr;
    LivePlayer*     m_player = nullptr;
    bool            m_bound  = false;
};

} // namespace uwp
