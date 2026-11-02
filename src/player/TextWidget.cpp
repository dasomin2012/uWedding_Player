#include "TextWidget.h"

#include <QColor>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFont>
#include <QFontMetricsF>
#include <QPainter>
#include <QPaintEvent>
#include <QTimer>
#include <cmath>

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

    // 애니메이션 타이머 — 스크롤(30fps) 또는 Clock(1Hz) 필요 시 활성.
    m_marqueeTimer = new QTimer(this);
    connect(m_marqueeTimer, &QTimer::timeout, this, [this]{ update(); });
    const bool fast = m_layer.scrollEnabled && m_layer.scrollSpeed > 0;
    const bool slow = (m_layer.mediaType == MediaType::Clock);
    if (fast)      { m_marqueeTimer->setInterval(33);   m_marqueeTimer->start(); }
    else if (slow) { m_marqueeTimer->setInterval(1000); m_marqueeTimer->start(); }
}

void TextWidget::setLayer(const Layer& layer) {
    m_layer = layer;
    const bool fast = layer.scrollEnabled && layer.scrollSpeed > 0;
    const bool slow = (layer.mediaType == MediaType::Clock);
    if (m_marqueeTimer) {
        if (fast)      { m_marqueeTimer->setInterval(33);   if (!m_marqueeTimer->isActive()) m_marqueeTimer->start(); }
        else if (slow) { m_marqueeTimer->setInterval(1000); if (!m_marqueeTimer->isActive()) m_marqueeTimer->start(); }
        else if (m_marqueeTimer->isActive()) m_marqueeTimer->stop();
    }
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
    // Clock 위젯은 현재 시각을 timeFormat 으로 계산 (매 paint 마다).
    const QString content = (m_layer.mediaType == MediaType::Clock)
        ? QDateTime::currentDateTime().toString(m_layer.timeFormat)
        : m_layer.text;

    if (m_layer.scrollEnabled && m_layer.scrollSpeed > 0) {
        static QElapsedTimer sClock;
        if (!sClock.isValid()) sClock.start();
        const qreal t = sClock.elapsed() / 1000.0;
        QFontMetricsF fm(f);
        const qreal textW = fm.horizontalAdvance(content);
        const qreal gap   = fm.horizontalAdvance(QStringLiteral("    "));
        const qreal cycle = qMax<qreal>(1.0, textW + gap);
        qreal offset = -std::fmod(t * m_layer.scrollSpeed, cycle);
        p.save();
        p.setClipRect(textRect);
        const qreal ty = textRect.center().y() + fm.ascent()/2.0 - fm.descent()/2.0;
        qreal x = textRect.left() + offset;
        while (x < textRect.right()) {
            p.drawText(QPointF(x, ty), content);
            x += cycle;
        }
        p.restore();
        return;
    }

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
    p.drawText(textRect, flags, content);
}

} // namespace uwp
