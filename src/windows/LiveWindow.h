#pragma once

#include <QWidget>
#include <QVector>
#include <QRect>
#include <functional>

#include "scene/Layer.h"
#include "live/ILiveSink.h"

class QTimer;

namespace uwp {

class VideoWidget;
class ImageWidget;
class LivePlayerPool;

// 확장 모니터 송출 윈도우 + 다중 레이어 컴포지터.
//
// 원자적 스왑: 새 씬을 옛 씬 "뒤"에 생성·재생 시작 → 모든 영상 레이어가
// 첫 프레임(has_vout)을 낼 때까지(타임아웃 보호) 대기 → 한 턴에 옛 씬
// 삭제 + 새 씬 전면화. 한 개씩 튀어나오는 현상 제거(진짜 Cut).
// onCommitted 콜백으로 전환효과(Fade)와 동기.
class LiveWindow : public QWidget, public ILiveSink {
    Q_OBJECT
public:
    explicit LiveWindow(LivePlayerPool* pool, QWidget* parent = nullptr);
    ~LiveWindow() override;

    void setCanvasSize(int width, int height) override;
    void showOnMonitor(int monitorIndex) override;

    // 씬을 스테이징→준비대기→원자적 commit. commit 완료 시 onCommitted 호출.
    void applyScene(const QVector<Layer>& layers,
                    std::function<void()> onCommitted = {}) override;

    QWidget* transitionAnchor() override { return this; }

    // Live 미러 스냅샷 — Win32 PrintWindow(PW_RENDERFULLCONTENT)로 VLC HWND
    // 포함 전체 클라이언트 영역을 캡처 후 maxWidthPx 이하로 스케일.
    // 동기 실행이지만 콜백 시그니처는 obs 백엔드와 동일하게 유지.
    void requestMirrorSnapshot(int maxWidthPx, MirrorCallback cb) override;

    bool playVideo(const QString& path);   // 단일 전체화면 영상 레이어
    void stopVideo();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;

private:
    struct LiveLayer {
        QString  id;
        QWidget* widget  = nullptr;
        Layer    layer;
        bool     isVideo = false;
    };

    LiveLayer buildLayer(const Layer& l);
    void  cancelPending();
    void  checkPendingReady();
    void  commitPending(bool byTimeout);
    void  clearList(QVector<LiveLayer>& list);
    void  relayoutList(QVector<LiveLayer>& list);
    QRect mapRect(const QRectF& canvasRect) const;   // "fit" 레터박스

    LivePlayerPool*        m_pool         = nullptr;
    QVector<LiveLayer>     m_layers;        // 현재 송출중
    QVector<LiveLayer>     m_pending;       // 스테이징중(옛 씬 뒤)
    std::function<void()>  m_onCommitted;
    QTimer*                m_readyTimer   = nullptr;
    int                    m_prepareElapsedMs = 0;
    int                    m_prepareTimeoutMs = 3000;   // priming 은 has_vout 보다 김
    int                    m_canvasWidth  = 1920;
    int                    m_canvasHeight = 1080;
    bool                   m_devMode      = false;
};

} // namespace uwp
