#include "TransitionEffect.h"

#include <QWidget>
#include <QPropertyAnimation>
#include <QPalette>
#include <QPointer>
#include <QScreen>
#include <QWindow>
#include <QDebug>

#ifdef _WIN32
#include <windows.h>
#endif

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

    // 출력 화면(만!)을 덮는 top-level 검은 오버레이. 스크린 모드에서 음수
    // 좌표(예: -5760, 0) 로 LiveWindow 가 있을 때 Windows 가 새 top-level 창을
    // 기본 primary 모니터로 잘못 재배치해 운용자 화면까지 어두워지는 회귀가
    // 있었음 → LiveWindow 와 같은 QScreen 에 명시 바인딩 + setGeometry 재적용.
    auto* overlay = new QWidget(nullptr,
        Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Tool);
    overlay->setAttribute(Qt::WA_DeleteOnClose);
    overlay->setAttribute(Qt::WA_ShowWithoutActivating);
    overlay->setFocusPolicy(Qt::NoFocus);
    overlay->setAutoFillBackground(true);
    QPalette pal = overlay->palette();
    pal.setColor(QPalette::Window, Qt::black);
    overlay->setPalette(pal);
    const QRect targetGeo = liveWindow->frameGeometry();
    overlay->setGeometry(targetGeo);
    overlay->setWindowOpacity(0.0);
    overlay->show();                                       // 네이티브 핸들 생성
    if (auto* wh = overlay->windowHandle()) {
        if (QScreen* s = liveWindow->screen())
            wh->setScreen(s);                              // 타깃 스크린 명시
    }
    overlay->setGeometry(targetGeo);                       // setScreen 후 정확 rect 재적용
    overlay->raise();

    // Qt 배치 우회: Qt::Tool 창은 Windows 가 음수 좌표(스크린 모드)를 정리해
    // primary 모니터로 강제 이동하는 경우가 있음. Win32 SetWindowPos 로 정확
    // 좌표에 재배치 + HWND_TOPMOST 로 z-order 확정. (모니터 모드는 fullscreen
    // 이라 이미 정확하지만 이 호출 자체는 무해.)
#ifdef _WIN32
    if (HWND hwnd = reinterpret_cast<HWND>(overlay->winId())) {
        SetWindowPos(hwnd, HWND_TOPMOST,
                     targetGeo.x(),     targetGeo.y(),
                     targetGeo.width(), targetGeo.height(),
                     SWP_NOACTIVATE);
    }
#endif

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
