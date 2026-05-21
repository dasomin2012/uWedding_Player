#include "LiveWindow.h"

#include "player/VideoWidget.h"
#include "player/ImageWidget.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QLabel>
#include <QTimer>
#include <QPalette>
#include <QFileInfo>
#include <QtMath>
#include <QDebug>
#include <algorithm>

namespace uwp {

LiveWindow::LiveWindow(LivePlayerPool* pool, QWidget* parent)
    : QWidget(parent)
    , m_pool(pool)
{
    setWindowTitle("uWeddingPlayer — Live");
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);

    m_readyTimer = new QTimer(this);
    m_readyTimer->setInterval(40);
    connect(m_readyTimer, &QTimer::timeout, this, &LiveWindow::checkPendingReady);
}

LiveWindow::~LiveWindow() {
    cancelPending();
    clearList(m_layers);
}

void LiveWindow::setCanvasSize(int width, int height) {
    m_canvasWidth  = qMax(1, width);
    m_canvasHeight = qMax(1, height);
    relayoutList(m_layers);
    relayoutList(m_pending);
}

// ---- 캔버스 → 출력 매핑 ("fit" 레터박스) -----------------------
QRect LiveWindow::mapRect(const QRectF& cr) const {
    const double W = width(), H = height();
    const double cw = m_canvasWidth, ch = m_canvasHeight;
    if (cw <= 0 || ch <= 0 || W <= 0 || H <= 0) return QRect();
    const double s  = qMin(W / cw, H / ch);
    const double ox = (W - cw * s) / 2.0;
    const double oy = (H - ch * s) / 2.0;
    return QRect(qRound(ox + cr.x() * s),
                 qRound(oy + cr.y() * s),
                 qMax(1, qRound(cr.width()  * s)),
                 qMax(1, qRound(cr.height() * s)));
}

// ---- 레이어 위젯 생성 ------------------------------------------
LiveWindow::LiveLayer LiveWindow::buildLayer(const Layer& l) {
    LiveLayer ll;
    ll.id    = l.id;
    ll.layer = l;

    switch (l.mediaType) {
    case MediaType::Video: {
        auto* vw = new VideoWidget(m_pool, this);
        vw->setGeometry(mapRect(l.geometry));
        vw->show();                                  // HWND 매핑 → vout 생성
        vw->play(l.media, l.endAction == EndAction::Loop);
        ll.widget  = vw;
        ll.isVideo = true;
        break;
    }
    case MediaType::Image: {
        auto* iw = new ImageWidget(this);
        iw->setGeometry(mapRect(l.geometry));
        iw->load(l.media);
        iw->show();
        ll.widget = iw;
        break;
    }
    default: {  // Document / Unknown → placeholder (Phase 8 실렌더)
        auto* ph = new QLabel(this);
        ph->setAttribute(Qt::WA_NativeWindow);
        ph->setAlignment(Qt::AlignCenter);
        ph->setStyleSheet("background:#101014; color:#667; border:1px solid #334;");
        ph->setText(QString("[document]\n%1").arg(QFileInfo(l.media).fileName()));
        ph->setGeometry(mapRect(l.geometry));
        ph->show();
        ll.widget = ph;
        break;
    }
    }
    return ll;
}

void LiveWindow::clearList(QVector<LiveLayer>& list) {
    for (LiveLayer& ll : list)
        if (ll.widget) { ll.widget->hide(); ll.widget->deleteLater(); }
    list.clear();
}

void LiveWindow::cancelPending() {
    if (m_readyTimer) m_readyTimer->stop();
    clearList(m_pending);
    m_onCommitted = nullptr;
}

// ---- 스테이징 (옛 씬 뒤에 새 씬 생성) --------------------------
void LiveWindow::applyScene(const QVector<Layer>& layers,
                            std::function<void()> onCommitted) {
    // 진행중 스테이징이 있으면 폐기 (rapid take 방어)
    cancelPending();

    QVector<Layer> ls = layers;
    std::stable_sort(ls.begin(), ls.end(),
                     [](const Layer& a, const Layer& b){ return a.zIndex < b.zIndex; });

    for (const Layer& l : ls)
        m_pending.push_back(buildLayer(l));

    // 옛 씬을 위로 올려 스테이징을 가린다 (준비될 때까지 옛 씬 유지)
    for (LiveLayer& ll : m_layers)
        if (ll.widget) ll.widget->raise();

    m_onCommitted      = std::move(onCommitted);
    m_prepareElapsedMs = 0;

    qInfo() << "LiveWindow::applyScene staging" << m_pending.size()
            << "layer(s) behind current" << m_layers.size();

    if (m_pending.isEmpty()) { commitPending(false); return; }
    m_readyTimer->start();
}

void LiveWindow::checkPendingReady() {
    m_prepareElapsedMs += m_readyTimer->interval();

    bool ready = true;
    for (const LiveLayer& ll : m_pending) {
        if (!ll.isVideo) continue;
        if (auto* vw = qobject_cast<VideoWidget*>(ll.widget)) {
            if (!vw->isReady()) { ready = false; break; }
        }
    }

    if (ready) {
        m_readyTimer->stop();
        // 모든 영상이 첫 프레임을 들고 있음 → 정지(cue)하여 노출 직전까지
        // 프레임이 진행/발산하지 않게 고정. 노출 후 동시 resume.
        for (LiveLayer& ll : m_pending)
            if (ll.isVideo)
                if (auto* vw = qobject_cast<VideoWidget*>(ll.widget)) vw->freeze();
        commitPending(false);
    }
    else if (m_prepareElapsedMs >= m_prepareTimeoutMs) {
        m_readyTimer->stop();
        commitPending(true);
    }
}

// ---- 원자적 commit (옛 씬 삭제 + 새 씬 전면화) ----------------
void LiveWindow::commitPending(bool byTimeout) {
    clearList(m_layers);                 // 옛 씬 제거
    m_layers   = m_pending;
    m_pending.clear();

    for (LiveLayer& ll : m_layers) {     // z 오름차순 → 마지막이 최상단
        if (ll.widget) { ll.widget->show(); ll.widget->raise(); }
    }

    // 노출 완료 → 정지(cue)된 영상들을 동시에 재개 → 동기 재생
    for (LiveLayer& ll : m_layers)
        if (ll.isVideo)
            if (auto* vw = qobject_cast<VideoWidget*>(ll.widget)) vw->resume();

    int v = 0, i = 0, d = 0;
    for (const LiveLayer& ll : m_layers) {
        if (ll.isVideo) ++v;
        else if (qobject_cast<ImageWidget*>(ll.widget)) ++i;
        else ++d;
    }
    qInfo() << "LiveWindow: committed" << m_layers.size()
            << "layer(s) atomically (video:" << v << "image:" << i
            << "doc:" << d << ")"
            << (byTimeout ? "[readiness timeout]" : "[all ready]");

    auto cb = m_onCommitted;
    m_onCommitted = nullptr;
    if (cb) cb();
}

bool LiveWindow::playVideo(const QString& path) {
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        qWarning() << "LiveWindow::playVideo: not found -" << path;
        return false;
    }
    Layer l;
    l.id        = "test";
    l.media     = path;
    l.mediaType = MediaType::Video;
    l.geometry  = QRectF(0, 0, m_canvasWidth, m_canvasHeight);
    l.zIndex    = 0;
    l.endAction = EndAction::Loop;
    applyScene({ l });
    return true;
}

void LiveWindow::stopVideo() {
    cancelPending();
    clearList(m_layers);
}

void LiveWindow::relayoutList(QVector<LiveLayer>& list) {
    for (LiveLayer& ll : list)
        if (ll.widget) ll.widget->setGeometry(mapRect(ll.layer.geometry));
}

// ---- 모니터 배치 ----------------------------------------------
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

    m_devMode = (screens.size() <= 1);
    QScreen* target = screens.at(idx);
    const QRect g   = target->geometry();

    // ★ 이미 표시중(특히 fullscreen)이면 먼저 일반 상태로 해제해야 다른
    //   모니터로 이동이 반영된다. fullscreen 상태에서는 setGeometry/ setScreen
    //   이 무시되어 기존 모니터에 그대로 남는다.
    if (isVisible()) showNormal();

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
        show();                                   // 네이티브 핸들 보장
        if (auto* wh = windowHandle()) wh->setScreen(target);  // 타깃 스크린 지정
        setGeometry(g);                           // setScreen 후 정확히 타깃 rect 로
        showFullScreen();                         // 타깃 모니터에서 fullscreen
    }
    relayoutList(m_layers);
    relayoutList(m_pending);

    qInfo() << "LiveWindow: shown on screen" << idx
            << target->name() << g.width() << "x" << g.height()
            << (m_devMode ? "(dev mode)" : "(fullscreen)");
}

void LiveWindow::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    relayoutList(m_layers);
    relayoutList(m_pending);
}

void LiveWindow::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape && isFullScreen()) {
        showNormal();
        setCursor(Qt::ArrowCursor);
        return;
    }
    QWidget::keyPressEvent(event);
}

} // namespace uwp
