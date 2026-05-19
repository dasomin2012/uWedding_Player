#pragma once

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

    // 씬을 스테이징 → 원자적 commit. commit 완료 시 onCommitted 호출.
    virtual void applyScene(const QVector<Layer>& layers,
                            std::function<void()> onCommitted = {}) = 0;

    // 전환 오버레이(dip-to-black)를 덮을 위젯.
    // 위젯 기반 백엔드만 자신을 반환하고, OBS 등 비위젯 백엔드는
    // nullptr 을 반환한다(TransitionEffect 가 즉시 apply 로 폴백).
    virtual QWidget* transitionAnchor() = 0;
};

} // namespace uwp
