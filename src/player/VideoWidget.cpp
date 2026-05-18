#include "VideoWidget.h"

#include "LivePlayerPool.h"

#include <QDebug>
#include <QPalette>

namespace uwp {

VideoWidget::VideoWidget(LivePlayerPool* pool, QWidget* parent)
    : QWidget(parent)
    , m_pool(pool)
{
    // libVLC 가 HWND 에 직접 그리도록 Qt paint 를 비활성화하고
    // 별도 네이티브 윈도우를 갖게 한다.
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setAutoFillBackground(false);

    if (m_pool) {
        m_player = m_pool->acquire();
    } else {
        qWarning() << "VideoWidget: no LivePlayerPool provided";
    }
}

VideoWidget::~VideoWidget() {
    if (m_pool && m_player) {
        m_pool->release(m_player);
        m_player = nullptr;
    }
}

void VideoWidget::bindSurface() {
    if (m_bound || !m_player) return;
    // winId() 호출 시점에 네이티브 윈도우가 보장됨 (WA_NativeWindow).
    m_player->setHwnd(reinterpret_cast<void*>(winId()));
    m_bound = true;
}

bool VideoWidget::play(const QString& path) {
    if (!m_player) {
        qWarning() << "VideoWidget::play: no player (stub/no SDK)";
        return false;
    }
    bindSurface();
    return m_player->play(path, /*loop=*/true);
}

void VideoWidget::stop() {
    if (m_player) m_player->stop();
}

void VideoWidget::pause() {
    if (m_player) m_player->pause();
}

bool VideoWidget::isPlaying() const {
    return m_player && m_player->isPlaying();
}

} // namespace uwp
