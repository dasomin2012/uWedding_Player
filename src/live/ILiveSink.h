#pragma once

#include <QImage>
#include <QVector>
#include <functional>

#include "scene/Layer.h"

class QWidget;

namespace uwp {

// Take 경로의 송출 백엔드 추상.
// Qt/libVLC(LiveWindow) 또는 OBS(ObsLiveBackend) 가 구현한다.
// 순수 추상(비 QObject) — QWidget 과 다중상속해도 moc 충돌 없음.
class ILiveSink {
public:
    virtual ~ILiveSink() = default;

    virtual void setCanvasSize(int width, int height) = 0;
    virtual void showOnMonitor(int monitorIndex) = 0;

    // 다음 applyScene 에 적용할 전환 의도. 위젯 백엔드는 무시(Qt
    // TransitionEffect 가 처리)하고, OBS 백엔드는 Studio Mode 전환에 사용.
    virtual void setTransition(bool /*fade*/, int /*fadeDurationMs*/) {}

    // 씬을 스테이징 → 원자적 commit. commit 완료 시 onCommitted 호출.
    virtual void applyScene(const QVector<Layer>& layers,
                            std::function<void()> onCommitted = {}) = 0;

    // 전환 오버레이(dip-to-black)를 덮을 위젯.
    // 위젯 기반 백엔드만 자신을 반환하고, OBS 등 비위젯 백엔드는
    // nullptr 을 반환한다(TransitionEffect 가 즉시 apply 로 폴백).
    virtual QWidget* transitionAnchor() = 0;

    // Live 미러(ControlWindow 우상단) 정지화상 요청.
    //   maxWidthPx : 미러 표시 폭(장축). 백엔드는 이 폭 이하로 스케일하여 반환.
    //   cb         : 결과 콜백. 실패 시 QImage() 로 1회 호출. 미지원 백엔드는
    //                기본 구현이 즉시 QImage() 로 호출한다.
    // qt 백엔드: 동기 (Win32 PrintWindow), obs 백엔드: 비동기 (websocket).
    using MirrorCallback = std::function<void(const QImage&)>;
    virtual void requestMirrorSnapshot(int /*maxWidthPx*/, MirrorCallback cb) {
        if (cb) cb(QImage{});
    }
};

} // namespace uwp
