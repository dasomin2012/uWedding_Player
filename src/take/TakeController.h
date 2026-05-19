#pragma once

#include <QObject>

#include "TransitionEffect.h"

namespace uwp {

class SceneModel;
class ILiveSink;
class Settings;

// Preview SceneModel 을 Live 로 commit 하는 주체.
// take() 시점에 SceneModel 레이어를 값 복사(스냅샷)하므로 이후 편집이
// Live 에 간섭하지 않는다(아키텍처 원칙을 모델 레벨에서도 보장).
class TakeController : public QObject {
    Q_OBJECT
public:
    TakeController(SceneModel* scene, ILiveSink* live, Settings* settings,
                   QObject* parent = nullptr);

    TransitionEffect::Mode mode() const { return m_mode; }

    // 런타임 sink 교체용 (O6-A: OBS Failed 시 qt 폴백 등).
    // 캔버스/전환모드 재동기화는 호출 측 책임.
    void setSink(ILiveSink* live);

public slots:
    void setMode(TransitionEffect::Mode m);
    void take();                 // 현재 씬을 Live 로 송출

signals:
    void modeChanged(TransitionEffect::Mode m);
    void taken(int layerCount);

private:
    SceneModel*            m_scene = nullptr;
    ILiveSink*             m_live  = nullptr;
    Settings*              m_settings = nullptr;
    TransitionEffect::Mode m_mode  = TransitionEffect::Mode::Fade;
    TransitionEffect       m_transition;
};

} // namespace uwp
