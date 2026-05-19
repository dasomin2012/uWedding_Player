#pragma once

#include <QWidget>
#include <QImage>
#include <QString>

namespace uwp {

// Live 측 이미지 레이어. 네이티브 윈도우(WA_NativeWindow)로 만들어
// libVLC 영상 위젯들과 동일한 네이티브 형제 z-order 규칙을 따른다.
// 이미지를 위젯 박스에 채워 그린다(LayerItem 미리보기와 시각 일치).
class ImageWidget : public QWidget {
    Q_OBJECT
public:
    explicit ImageWidget(QWidget* parent = nullptr);

    bool load(const QString& path);

protected:
    void paintEvent(QPaintEvent* e) override;

private:
    QImage  m_image;
    QString m_path;
};

} // namespace uwp
