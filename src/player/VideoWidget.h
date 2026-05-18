#pragma once

#include <QWidget>
#include <QString>
#include <memory>

namespace uwp {

// libVLC 기반 비디오 재생 위젯.
// libVLC SDK 미탑재 시 stub 으로 빌드되어 항상 false 반환 (검은 배경 유지).
class VideoWidget : public QWidget {
    Q_OBJECT
public:
    explicit VideoWidget(QWidget* parent = nullptr);
    ~VideoWidget() override;

    // 파일을 로드하고 즉시 재생 시작. Phase 1: 무한 루프.
    bool play(const QString& path);
    void stop();
    bool isPlaying() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace uwp
