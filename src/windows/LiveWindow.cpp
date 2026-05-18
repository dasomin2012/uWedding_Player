#include "LiveWindow.h"

#include "player/VideoWidget.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QPalette>
#include <QDebug>

namespace uwp {

LiveWindow::LiveWindow(LivePlayerPool* pool, QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle("uWeddingPlayer — Live");

    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    m_videoWidget = new VideoWidget(pool, this);
    m_videoWidget->setGeometry(0, 0, m_canvasWidth, m_canvasHeight);
}

LiveWindow::~LiveWindow() = default;

void LiveWindow::setCanvasSize(int width, int height) {
    m_canvasWidth  = width;
    m_canvasHeight = height;
    if (m_videoWidget) {
        // Phase 1: 일단 윈도우 전체 영역을 차지. 정밀한 fit/fill 스케일은 후속.
        m_videoWidget->setGeometry(rect());
    }
}

void LiveWindow::showOnMonitor(int monitorIndex) {
    const auto screens = QGuiApplication::screens();
    if (screens.isEmpty()) {
        qWarning() << "LiveWindow: no screens detected";
        return;
    }

    int idx = monitorIndex;
    if (idx < 0 || idx >= screens.size()) {
        const int fallback = (screens.size() > 1) ? screens.size() - 1 : 0;
        qWarning() << "LiveWindow: output_monitor_index" << monitorIndex
                   << "out of range (screens =" << screens.size()
                   << ") — falling back to screen" << fallback;
        idx = fallback;
    }

    // 보조 모니터가 없으면 dev 모드(창 모드)로 표시
    m_devMode = (screens.size() <= 1);
    QScreen* target = screens.at(idx);
    const QRect g   = target->geometry();

    if (m_devMode) {
        qInfo() << "LiveWindow: single monitor only — dev windowed mode 1280x720";
        setWindowFlags(Qt::Window);
        setCursor(Qt::ArrowCursor);
        resize(1280, 720);
        move(g.x() + 100, g.y() + 100);
        show();
    } else {
        setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
        setCursor(Qt::BlankCursor);
        setGeometry(g);
        show();
        if (auto* wh = windowHandle()) {
            wh->setScreen(target);
        }
        showFullScreen();
    }

    qInfo() << "LiveWindow: shown on screen" << idx
            << target->name() << g.width() << "x" << g.height()
            << (m_devMode ? "(dev mode)" : "(fullscreen)");
}

bool LiveWindow::playVideo(const QString& path) {
    return m_videoWidget ? m_videoWidget->play(path) : false;
}

void LiveWindow::stopVideo() {
    if (m_videoWidget) m_videoWidget->stop();
}

void LiveWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_videoWidget) {
        m_videoWidget->setGeometry(rect());
    }
}

void LiveWindow::keyPressEvent(QKeyEvent* event) {
    // Phase 1 개발 편의: ESC 로 fullscreen 탈출
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        showNormal();
        setCursor(Qt::ArrowCursor);
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace uwp
