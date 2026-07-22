#include "TextWidget.h"

#include <QColor>
#include <QFont>
#include <QPainter>
#include <QPaintEvent>

namespace uwp {

TextWidget::TextWidget(const Layer& layer, QWidget* parent)
    : QWidget(parent)
    , m_layer(layer)
{
    // 다른 Live 위젯(VideoWidget/ImageWidget)과 동일한 네이티브 형제 규칙.
    setAttribute(Qt::WA_NativeWindow);
    // 배경은 paintEvent 가 opacity 를 반영해 직접 그린다 → auto-fill 방지.
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setAutoFillBackground(false);
}

void TextWidget::setLayer(const Layer& layer) {
    m_layer = layer;
    update();
}

void TextWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF box = rect();

    // 배경 (opacity 반영)
    QColor bg(m_layer.bgColor);
    bg.setAlphaF(qBound(0.0, m_layer.bgOpacity, 1.0));
    p.fillRect(box, bg);

    // 텍스트
    QFont f(m_layer.fontFamily);
    f.setPixelSize(qMax(1, m_layer.fontSize));
    f.setWeight(m_layer.fontWeight >= 700 ? QFont::Bold : QFont::Normal);
    p.setFont(f);
    p.setPen(QColor(m_layer.textColor));

    const int pad = qMax(0, m_layer.padding);
    const QRectF textRect = box.adjusted(pad, pad, -pad, -pad);

    // Layer.textAlign / textVAlign: 0=Left/Top, 1=Center, 2=Right/Bottom
    int horiz = Qt::AlignHCenter;
    switch (m_layer.textAlign) {
        case 0: horiz = Qt::AlignLeft;    break;
        case 2: horiz = Qt::AlignRight;   break;
        default: horiz = Qt::AlignHCenter;
    }
    int vert = Qt::AlignVCenter;
    switch (m_layer.textVAlign) {
        case 0: vert = Qt::AlignTop;      break;
        case 2: vert = Qt::AlignBottom;   break;
        default: vert = Qt::AlignVCenter;
    }
    const int flags = Qt::TextWordWrap | horiz | vert;
    p.drawText(textRect, flags, m_layer.text);
}

} // namespace uwp
