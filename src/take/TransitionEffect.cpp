#include "TransitionEffect.h"

#include <QWidget>
#include <QPropertyAnimation>
#include <QPalette>
#include <QPointer>
#include <QDebug>

namespace uwp {

TransitionEffect::TransitionEffect(QObject* parent) : QObject(parent) {}

void TransitionEffect::run(Mode mode, int durationMs, QWidget* liveWindow,
                           std::function<void(std::function<void()>)> applyAsync) {
    if (!applyAsync) { m_running = false; return; }

    if (m_running) {
        qWarning() << "TransitionEffect: busy — applying without transition";
        applyAsync([]{});
        return;
    }

    if (mode == Mode::Cut || durationMs <= 0 || !liveWindow) {
        // Cut: 옛 씬 유지 → 새 씬 준비완료 시 LiveWindow 가 원자적 스왑.
        applyAsync([]{});
        return;
    }

    m_running = true;

    // 출력 화면을 덮는 top-level 검은 오버레이
    auto* overlay = new QWidget(nullptr,
        Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    overlay->setAttribute(Qt::WA_DeleteOnClose);
    overlay->setAttribute(Qt::WA_ShowWithoutActivating);
    overlay->setFocusPolicy(Qt::NoFocus);
    overlay->setAutoFillBackground(true);
    QPalette pal = overlay->palette();
    pal.setColor(QPalette::Window, Qt::black);
    overlay->setPalette(pal);
    overlay->setGeometry(liveWindow->geometry());
    overlay->setWindowOpacity(0.0);
    overlay->show();
    overlay->raise();

    const int half = qMax(1, durationMs / 2);
    QPointer<QWidget> ovp(overlay);

    auto* fadeIn = new QPropertyAnimation(overlay, "windowOpacity", this);
    fadeIn->setDuration(half);
    fadeIn->setStartValue(0.0);
    fadeIn->setEndValue(1.0);

    connect(fadeIn, &QPropertyAnimation::finished, this,
            [this, ovp, half, applyAsync]() {
        // 화면이 완전히 검은 상태. 새 씬 스테이징 시작 →
        // commit(준비완료) 시 done 콜백에서 fadeOut.
        applyAsync([this, ovp, half]() {
            if (!ovp) { m_running = false; return; }
            auto* fadeOut = new QPropertyAnimation(ovp, "windowOpacity", this);
            fadeOut->setDuration(half);
            fadeOut->setStartValue(1.0);
            fadeOut->setEndValue(0.0);
            connect(fadeOut, &QPropertyAnimation::finished, this, [this, ovp]() {
                if (ovp) ovp->close();          // WA_DeleteOnClose
                m_running = false;
            });
            fadeOut->start(QAbstractAnimation::DeleteWhenStopped);
        });
    });

    fadeIn->start(QAbstractAnimation::DeleteWhenStopped);
    qInfo() << "TransitionEffect: fade (dip-to-black)" << durationMs
            << "ms — fadeIn" << half << "ms, hold-black until scene ready, fadeOut"
            << half << "ms";
}

} // namespace uwp
