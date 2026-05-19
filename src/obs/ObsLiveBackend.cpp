#include "ObsLiveBackend.h"

#include "ObsClient.h"
#include "ObsProcessManager.h"

#include <QJsonArray>
#include <QDebug>

namespace uwp {

ObsLiveBackend::ObsLiveBackend(ObsProcessManager* proc, const ObsConfig& cfg,
                               QObject* parent)
    : QObject(parent)
    , m_proc(proc)
    , m_sceneA(cfg.sceneA.isEmpty() ? QStringLiteral("UWP_PGM_A") : cfg.sceneA)
    , m_sceneB(cfg.sceneB.isEmpty() ? QStringLiteral("UWP_PGM_B") : cfg.sceneB)
{
    m_pendingCommit = []{};
    if (m_proc) {
        connect(m_proc, &ObsProcessManager::ready,
                this, &ObsLiveBackend::onObsReady);
    }
}

ObsLiveBackend::~ObsLiveBackend() = default;

ObsClient* ObsLiveBackend::client() const {
    return m_proc ? m_proc->client() : nullptr;
}

// ---- ILiveSink ------------------------------------------------
void ObsLiveBackend::setCanvasSize(int width, int height) {
    m_canvasW = width;
    m_canvasH = height;
    if (m_seeded && client()) {
        QJsonObject d;
        d["baseWidth"]   = m_canvasW;  d["baseHeight"]   = m_canvasH;
        d["outputWidth"] = m_canvasW;  d["outputHeight"] = m_canvasH;
        client()->request(QStringLiteral("SetVideoSettings"), d, {});
    }
}

void ObsLiveBackend::showOnMonitor(int monitorIndex) {
    // 프로젝터 오픈은 ObsProcessManager 소관. 여기서는 기록만.
    qInfo() << "ObsLiveBackend: showOnMonitor" << monitorIndex
            << "(projector owned by ObsProcessManager)";
}

void ObsLiveBackend::setTransition(bool fade, int fadeDurationMs) {
    m_fade   = fade;
    m_fadeMs = fadeDurationMs;
}

void ObsLiveBackend::applyScene(const QVector<Layer>& layers,
                                std::function<void()> onCommitted) {
    m_pendingLayers = layers;
    m_pendingCommit = onCommitted ? std::move(onCommitted)
                                  : std::function<void()>([]{});
    m_havePending   = true;

    if (m_seeded && client())
        doApply();
    else
        qInfo() << "ObsLiveBackend: applyScene queued (OBS not seeded yet)";
}

// ---- OBS 준비 / 시딩 ------------------------------------------
void ObsLiveBackend::onObsReady() {
    ObsClient* c = client();
    if (!c) return;
    if (!m_clientBound) {
        connect(c, &ObsClient::obsEvent, this, &ObsLiveBackend::onObsEvent);
        m_clientBound = true;
    }
    m_seeded = false;
    seed();
}

void ObsLiveBackend::seed() {
    ObsClient* c = client();
    if (!c) return;

    QJsonObject vid;
    vid["baseWidth"]     = m_canvasW;  vid["baseHeight"]     = m_canvasH;
    vid["outputWidth"]   = m_canvasW;  vid["outputHeight"]   = m_canvasH;
    vid["fpsNumerator"]  = 60;         vid["fpsDenominator"] = 1;

    c->request(QStringLiteral("SetVideoSettings"), vid,
        [this](bool ok, const QJsonObject&, const QString& cm) {
            if (!ok) qWarning() << "ObsLiveBackend: SetVideoSettings —" << cm;
            ObsClient* c2 = client();
            if (!c2) return;
            // 씬 보장
            c2->request(QStringLiteral("GetSceneList"), {},
                [this](bool ok2, const QJsonObject& d, const QString&) {
                    ObsClient* c3 = client();
                    if (!c3) return;
                    QStringList have;
                    if (ok2)
                        for (const QJsonValue& v :
                             d.value("scenes").toArray())
                            have << v.toObject().value("sceneName").toString();
                    for (const QString& s : { m_sceneA, m_sceneB })
                        if (!have.contains(s)) {
                            QJsonObject cs; cs["sceneName"] = s;
                            c3->request(QStringLiteral("CreateScene"), cs, {});
                        }
                    QJsonObject sm; sm["studioModeEnabled"] = true;
                    c3->request(QStringLiteral("SetStudioModeEnabled"), sm,
                        [this](bool, const QJsonObject&, const QString&) {
                            m_seeded = true;
                            qInfo() << "ObsLiveBackend: seeded "
                                       "(canvas/scenes/studio-mode)";
                            if (m_havePending) doApply();
                        });
                });
        });
}

// ---- 적용 (off-air 씬 재구성 → 전환) --------------------------
void ObsLiveBackend::doApply() {
    if (!client()) return;
    const QString target =
        (m_programScene == m_sceneA) ? m_sceneB : m_sceneA;
    const QVector<Layer> layers = m_pendingLayers;
    m_havePending = false;

    qInfo() << "ObsLiveBackend: rebuilding off-air scene" << target
            << "with" << layers.size() << "layer(s)";
    rebuildScene(target, layers, [this, target]() {
        triggerTransition(target);
    });
}

void ObsLiveBackend::rebuildScene(const QString& scene,
                                  const QVector<Layer>& layers,
                                  std::function<void()> done) {
    ObsClient* c = client();
    if (!c) { done(); return; }

    const QStringList old = m_inputsByScene.value(scene);
    m_inputsByScene[scene].clear();

    if (old.isEmpty()) {
        buildLayer(scene, layers, 0, std::move(done));
        return;
    }
    auto remaining = std::make_shared<int>(old.size());
    for (const QString& name : old) {
        QJsonObject d; d["inputName"] = name;
        c->request(QStringLiteral("RemoveInput"), d,
            [this, scene, layers, done, remaining]
            (bool, const QJsonObject&, const QString&) {
                if (--(*remaining) > 0) return;
                buildLayer(scene, layers, 0, done);
            });
    }
}

void ObsLiveBackend::buildLayer(const QString& scene, QVector<Layer> layers,
                                int i, std::function<void()> done) {
    if (i >= layers.size()) { done(); return; }
    ObsClient* c = client();
    if (!c) { done(); return; }

    const Layer L = layers[i];
    auto next = [this, scene, layers, i, done]() {
        buildLayer(scene, layers, i + 1, done);
    };

    QString kind;
    QJsonObject settings;
    if (L.mediaType == MediaType::Video) {
        kind = QStringLiteral("ffmpeg_source");
        settings["local_file"]          = L.media;
        settings["is_local_file"]       = true;
        settings["looping"]             = (L.endAction == EndAction::Loop);
        settings["restart_on_activate"] = true;
        settings["hw_decode"]           = true;
    } else if (L.mediaType == MediaType::Image) {
        kind = QStringLiteral("image_source");
        settings["file"] = L.media;
    } else {
        qWarning() << "ObsLiveBackend: skipping unsupported layer"
                   << L.id << "(type" << static_cast<int>(L.mediaType)
                   << "— Document/PPT OBS 연동은 후속)";
        next();
        return;
    }

    const QString inputName =
        QStringLiteral("uwp_%1_%2").arg(scene, L.id);
    m_inputsByScene[scene] << inputName;

    QJsonObject ci;
    ci["sceneName"]        = scene;
    ci["inputName"]        = inputName;
    ci["inputKind"]        = kind;
    ci["inputSettings"]    = settings;
    ci["sceneItemEnabled"] = true;

    c->request(QStringLiteral("CreateInput"), ci,
        [this, scene, inputName, L, i, next](bool ok, const QJsonObject& d,
                                             const QString& cm) {
            ObsClient* c2 = client();
            if (!ok || !c2) {
                qWarning() << "ObsLiveBackend: CreateInput failed for"
                           << L.id << "—" << cm;
                next();
                return;
            }
            const int itemId = d.value("sceneItemId").toInt();

            QJsonObject tr;
            tr["positionX"]       = L.geometry.x();
            tr["positionY"]       = L.geometry.y();
            tr["boundsType"]      = QStringLiteral("OBS_BOUNDS_STRETCH");
            tr["boundsAlignment"] = 5;  // top-left
            tr["boundsWidth"]     = L.geometry.width();
            tr["boundsHeight"]    = L.geometry.height();

            QJsonObject st;
            st["sceneName"]          = scene;
            st["sceneItemId"]        = itemId;
            st["sceneItemTransform"] = tr;
            c2->request(QStringLiteral("SetSceneItemTransform"), st,
                [this, scene, inputName, itemId, i, L, next]
                (bool, const QJsonObject&, const QString&) {
                    ObsClient* c3 = client();
                    if (!c3) { next(); return; }
                    QJsonObject idx;
                    idx["sceneName"]      = scene;
                    idx["sceneItemId"]    = itemId;
                    idx["sceneItemIndex"] = i;  // 레이어는 zIndex 오름차순
                    c3->request(QStringLiteral("SetSceneItemIndex"), idx,
                        [this, inputName, L, next]
                        (bool, const QJsonObject&, const QString&) {
                            ObsClient* c4 = client();
                            // opacity best-effort: 입력에 색보정 필터 추가.
                            // 필터 종류는 OBS 버전 의존 → 실패해도 무시.
                            if (c4 && L.opacity < 0.999) {
                                QJsonObject fs; fs["opacity"] = L.opacity;
                                QJsonObject f;
                                f["sourceName"] = inputName;
                                f["filterName"] = QStringLiteral("uwp_opacity");
                                f["filterKind"] =
                                    QStringLiteral("color_filter_v2");
                                f["filterSettings"] = fs;
                                c4->request(
                                    QStringLiteral("CreateSourceFilter"),
                                    f, {});
                            }
                            next();
                        });
                });
        });
}

void ObsLiveBackend::triggerTransition(const QString& targetScene) {
    ObsClient* c = client();
    if (!c) return;

    QJsonObject tn;
    tn["transitionName"] = m_fade ? QStringLiteral("Fade")
                                  : QStringLiteral("Cut");
    c->request(QStringLiteral("SetCurrentSceneTransition"), tn,
        [this, targetScene](bool, const QJsonObject&, const QString&) {
            ObsClient* c2 = client();
            if (!c2) return;
            auto afterDur = [this, targetScene]() {
                ObsClient* c3 = client();
                if (!c3) return;
                QJsonObject ps; ps["sceneName"] = targetScene;
                c3->request(QStringLiteral("SetCurrentPreviewScene"), ps,
                    [this, targetScene](bool, const QJsonObject&,
                                        const QString&) {
                        ObsClient* c4 = client();
                        if (!c4) return;
                        m_inFlightCommit = m_pendingCommit;
                        m_programScene   = targetScene;
                        c4->request(
                            QStringLiteral("TriggerStudioModeTransition"),
                            {}, {});
                        qInfo() << "ObsLiveBackend: transition →"
                                << targetScene
                                << (m_fade ? "(fade)" : "(cut)");
                    });
            };
            if (m_fade) {
                QJsonObject td;
                td["transitionDuration"] = m_fadeMs;
                c2->request(
                    QStringLiteral("SetCurrentSceneTransitionDuration"), td,
                    [afterDur](bool, const QJsonObject&, const QString&) {
                        afterDur();
                    });
            } else {
                afterDur();
            }
        });
}

// ---- 이벤트 ---------------------------------------------------
void ObsLiveBackend::onObsEvent(const QString& eventType,
                                const QJsonObject&) {
    if (eventType == QLatin1String("SceneTransitionEnded")) {
        if (m_inFlightCommit) {
            auto cb = m_inFlightCommit;
            m_inFlightCommit = nullptr;
            cb();
        }
        emit transitionEnded();
    }
}

} // namespace uwp
