#include "TakeController.h"

#include "scene/SceneModel.h"
#include "scene/Layer.h"
#include "live/ILiveSink.h"
#include "app/Settings.h"

#include <QVector>
#include <QDebug>

namespace uwp {

TakeController::TakeController(SceneModel* scene, ILiveSink* live,
                              Settings* settings, QObject* parent)
    : QObject(parent)
    , m_scene(scene)
    , m_live(live)
    , m_settings(settings)
    , m_transition(this)
{
    const QString dm = settings ? settings->takeDefaultMode().toLower()
                                : QString("fade");
    m_mode = (dm == "cut") ? TransitionEffect::Mode::Cut
                           : TransitionEffect::Mode::Fade;
}

void TakeController::setMode(TransitionEffect::Mode m) {
    if (m_mode == m) return;
    m_mode = m;
    emit modeChanged(m_mode);
}

void TakeController::take() {
    if (!m_scene || !m_live) return;

    // 값 복사 스냅샷 — 이후 편집과 분리
    const QVector<Layer> snapshot = m_scene->layers();
    const int durationMs = m_settings ? m_settings->takeFadeDurationMs() : 800;

    qInfo() << "TakeController::take mode="
            << (m_mode == TransitionEffect::Mode::Cut ? "cut" : "fade")
            << "layers=" << snapshot.size();

    // OBS 백엔드용 전환 의도 전달(위젯 백엔드는 no-op).
    m_live->setTransition(m_mode == TransitionEffect::Mode::Fade, durationMs);

    ILiveSink* live = m_live;
    m_transition.run(
        m_mode,
        (m_mode == TransitionEffect::Mode::Fade) ? durationMs : 0,
        live->transitionAnchor(),
        [live, snapshot](std::function<void()> done) {
            live->applyScene(snapshot, std::move(done));
        });

    // NovaStar 프리셋 동기 호출은 Phase 6 (presetId 매핑 시).
    emit taken(snapshot.size());
}

} // namespace uwp
