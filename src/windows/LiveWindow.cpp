#include "LiveWindow.h"

#include "player/VideoWidget.h"
#include "player/ImageWidget.h"
#include "player/TextWidget.h"

#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QKeyEvent>
#include <QResizeEvent>
#include <QLabel>
#include <QTimer>
#include <QPainter>
#include <QPaintEvent>
#include <QPalette>
#include <QFileInfo>
#include <QtMath>
#include <QDebug>
#include <QImage>
#include <QPixmap>
#include <algorithm>

#ifdef Q_OS_WIN
#include <windows.h>
#ifndef PW_RENDERFULLCONTENT
#define PW_RENDERFULLCONTENT 0x00000002   // Win 8.1+; VLC HWND 자식창 포함 캡처
#endif
#endif

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
    case MediaType::Text: {
        auto* tw = new TextWidget(l, this);
        tw->setGeometry(mapRect(l.geometry));
        tw->show();
        ll.widget = tw;
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

    // BLACK/Stop 처럼 새 씬이 비면 자식 native HWND 를 지운 것만으로는
    // 부모 QWidget 이 자동 재도색되지 않아 이전 프레임이 남는 경우가 있음
    // (WA_OpaquePaintEvent + native child 조합의 Windows-side 아티팩트).
    // 명시적으로 재도색 강제 → 팔레트의 검정 배경이 즉시 노출된다.
    if (m_layers.isEmpty()) update();

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

// ---- 스크린 모드 (임의 좌표/크기 frameless) --------------------
// showOnMonitor 는 fullscreen 이지만, 여기는 데스크톱 가상 좌표계 위 임의
// 사각형에 배치. OBS 백엔드의 setProjectorGeometry 와 UX 대응 — LED 스크린
// 이 특정 모니터의 부분 영역이거나 여러 모니터에 걸친 웨딩홀 세팅용.
//
// fullscreen 이 아니라 일반 창이지만 FramelessWindowHint 로 테두리 제거 →
// 지정 좌표/크기에 픽셀 1:1. Preview 캔버스와 같은 논리 캔버스 크기면
// mapRect 가 identity 로 동작해 왜곡 없음.
void LiveWindow::showAtGeometry(int x, int y, int width, int height) {
    if (width < 1 || height < 1) {
        qWarning() << "LiveWindow::showAtGeometry: invalid size"
                   << width << "x" << height;
        return;
    }
    // fullscreen 잔재 정리 — showOnMonitor 와 동일 이유.
    if (isVisible()) showNormal();

    setWindowFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setCursor(Qt::BlankCursor);
    const QRect g(x, y, width, height);
    setGeometry(g);
    show();
    // 좌표에 해당하는 화면(있으면)에 windowHandle 명시 → 크로스 모니터일 때도
    // 표시가 확실. QGuiApplication::screenAt 은 좌표 → 스크린 조회.
    if (auto* wh = windowHandle()) {
        if (QScreen* s = QGuiApplication::screenAt(QPoint(x, y)))
            wh->setScreen(s);
    }
    setGeometry(g);  // setScreen 후 재확정 (좌표 클램프 회피)
    relayoutList(m_layers);
    relayoutList(m_pending);

    qInfo() << "LiveWindow: shown at geometry"
            << width << "x" << height << "@" << x << "," << y;
}

// 항상 검정으로 채움 — 자식 native HWND (VLC 비디오) 가 hide/destroy 될 때
// 그 영역이 이전 픽셀 그대로 남는 Windows-side 잔상 방지. WA_OpaquePaintEvent
// 를 유지한 채 명시 paintEvent 로 픽셀 확정.
void LiveWindow::paintEvent(QPaintEvent* event) {
    QPainter p(this);
    p.fillRect(event->rect(), Qt::black);
}

// 자동 진행 일시정지 — 현재 재생 중인 모든 영상 레이어를 현재 프레임에 정지.
// 이미지 레이어는 정적이라 그대로 두어도 시각적 변화 없음.
void LiveWindow::pauseAllVideos() {
    for (LiveLayer& ll : m_layers)
        if (ll.isVideo)
            if (auto* vw = qobject_cast<VideoWidget*>(ll.widget)) vw->freeze();
}

void LiveWindow::resumeAllVideos() {
    for (LiveLayer& ll : m_layers)
        if (ll.isVideo)
            if (auto* vw = qobject_cast<VideoWidget*>(ll.widget)) vw->resume();
}

// Screen OFF/ON 마스크 — 위젯을 파괴하지 않아 페이지 상태(영상 위치·이미지)
// 그대로 유지. hide() 는 native HWND(VLC)의 SW_HIDE 로 처리되어 검정 배경 노출.
void LiveWindow::setMasked(bool masked) {
    if (m_masked == masked) return;
    m_masked = masked;
    for (LiveLayer& ll : m_layers) {
        if (!ll.widget) continue;
        if (masked) ll.widget->hide();
        else        ll.widget->show();
    }
    if (masked) update();   // native HWND destroy 잔상 방지 (paintEvent 로 검정)
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

// ---- Live 미러 스냅샷 -------------------------------------------
// LiveWindow 는 자기 자식으로 VLC 네이티브 HWND(VideoWidget)를 가진다.
// QWidget::grab()/QScreen::grabWindow() 는 자식 네이티브 창을 검게 그린다.
// 해결: Win32 PrintWindow + PW_RENDERFULLCONTENT — OS 에게 "직접 그리게" 요청.
//   - Windows 8.1+ 지원. 그 전에는 VLC 부분이 검게 나오지만 fallback 없이 그대로.
//   - 4K 프레임을 매 400ms 마다 캡처해도 스케일 후 32KB 이하 QPixmap 이므로
//     UI 스레드 영향은 미미(측정치 ~2–4ms per capture on 4K).
void LiveWindow::requestMirrorSnapshot(int maxWidthPx, MirrorCallback cb) {
    if (!cb) return;

#ifdef Q_OS_WIN
    if (!isVisible()) { cb(QImage{}); return; }

    // 네이티브 HWND 확보. LiveWindow 는 top-level 이므로 winId()가 안전.
    HWND hwnd = reinterpret_cast<HWND>(winId());
    if (!hwnd) { cb(QImage{}); return; }

    RECT r{};
    if (!GetClientRect(hwnd, &r)) { cb(QImage{}); return; }
    const int w = r.right - r.left;
    const int h = r.bottom - r.top;
    if (w <= 0 || h <= 0) { cb(QImage{}); return; }

    HDC screenDc = GetDC(nullptr);
    HDC memDc    = CreateCompatibleDC(screenDc);
    HBITMAP bmp  = CreateCompatibleBitmap(screenDc, w, h);
    HGDIOBJ old  = SelectObject(memDc, bmp);

    QImage out;
    if (PrintWindow(hwnd, memDc, PW_RENDERFULLCONTENT)) {
        BITMAPINFO bi{};
        bi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth       = w;
        bi.bmiHeader.biHeight      = -h;      // top-down
        bi.bmiHeader.biPlanes      = 1;
        bi.bmiHeader.biBitCount    = 32;
        bi.bmiHeader.biCompression = BI_RGB;

        QImage full(w, h, QImage::Format_RGB32);
        if (!full.isNull()) {
            if (GetDIBits(memDc, bmp, 0, h, full.bits(), &bi, DIB_RGB_COLORS)) {
                // 미러 폭에 맞춰 즉시 스케일 → UI 넘길 이미지 크기 최소화
                const int targetW = qMax(1, qMin(maxWidthPx, w));
                out = full.scaledToWidth(targetW, Qt::SmoothTransformation);
            }
        }
    }

    SelectObject(memDc, old);
    DeleteObject(bmp);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);

    cb(out);
#else
    Q_UNUSED(maxWidthPx);
    cb(QImage{});
#endif
}

} // namespace uwp
