#include "ImageWidget.h"

#include <QPainter>
#include <QImageReader>
#include <QPalette>
#include <QDebug>

namespace uwp {

ImageWidget::ImageWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_NativeWindow);     // 영상 위젯과 동일 z-order 규칙
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setAutoFillBackground(false);
}

bool ImageWidget::load(const QString& path) {
    QImageReader r(path);
    r.setAutoTransform(true);
    m_image = r.read();
    m_path  = path;
    if (m_image.isNull()) {
        qWarning() << "ImageWidget::load failed:" << path << "-" << r.errorString();
        return false;
    }
    update();
    return true;
}

void ImageWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (m_image.isNull()) return;
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    // LayerItem 미리보기와 동일하게 박스에 맞춰 그림(스트레치)
    p.drawImage(rect(), m_image, m_image.rect());
}

} // namespace uwp
