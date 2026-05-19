#include "ObsLiveBackend.h"

#include "ObsClient.h"
#include "ObsProcessManager.h"

#include <QJsonArray>
#include <QDir>
#include <QFileInfo>
#include <QCoreApplication>
#include <QDebug>

#include <memory>

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
                    // Studio Mode 비활성 — program 을 SetCurrentProgramScene
                    // 으로 직접 ping-pong 전환(전환은 활성 scene transition).
                    QJsonObject sm; sm["studioModeEnabled"] = false;
                    c3->request(QStringLiteral("SetStudioModeEnabled"), sm,
                        [this](bool, const QJsonObject&, const QString&) {
                            ObsClient* ct = client();
                            if (!ct) return;
                            // 전환을 kind 로 탐색(로케일 무관). cut/fade 의
                            // 표시이름을 저장해 둔다.
                            ct->request(
                                QStringLiteral("GetSceneTransitionList"), {},
                                [this](bool, const QJsonObject& dt,
                                       const QString&) {
                            for (const QJsonValue& v :
                                 dt.value("transitions").toArray()) {
                                const QJsonObject t = v.toObject();
                                const QString k =
                                    t.value("transitionKind").toString();
                                const QString n =
                                    t.value("transitionName").toString();
                                if (k == QLatin1String("cut_transition")
                                    && m_cutName.isEmpty())
                                    m_cutName = n;
                                if (k == QLatin1String("fade_transition")
                                    && m_fadeName.isEmpty())
                                    m_fadeName = n;
                            }
                            qInfo() << "ObsLiveBackend: transitions cut="
                                    << m_cutName << "fade=" << m_fadeName;
                            // 이전 세션/크래시가 포터블 config 에 남긴 uwp_*
                            // 입력을 일괄 제거 → 결정적 이름 충돌 원천 차단.
                            ObsClient* c4 = client();
                            auto finish = [this]() {
                                m_inputsByScene.clear();
                                m_seeded = true;
                                qInfo() << "ObsLiveBackend: seeded "
                                           "(canvas/scenes/transitions, "
                                           "stale purged)";
                                if (m_havePending) doApply();
                            };
                            if (!c4) { finish(); return; }
                            c4->request(QStringLiteral("GetInputList"), {},
                                [this, finish](bool, const QJsonObject& d,
                                               const QString&) {
                                    ObsClient* c5 = client();
                                    QStringList stale;
                                    for (const QJsonValue& v :
                                         d.value("inputs").toArray()) {
                                        const QString n = v.toObject()
                                            .value("inputName").toString();
                                        if (n.startsWith(
                                                QLatin1String("uwp_")))
                                            stale << n;
                                    }
                                    if (!c5 || stale.isEmpty()) {
                                        finish();
                                        return;
                                    }
                                    qInfo() << "ObsLiveBackend: purging"
                                            << stale.size()
                                            << "stale uwp_ input(s)";
                                    auto left =
                                        std::make_shared<int>(stale.size());
                                    for (const QString& n : stale) {
                                        QJsonObject rm; rm["inputName"] = n;
                                        c5->request(
                                            QStringLiteral("RemoveInput"), rm,
                                            [left, finish]
                                            (bool, const QJsonObject&,
                                             const QString&) {
                                                if (--(*left) == 0) finish();
                                            });
                                    }
                                });
                                });   // GetSceneTransitionList
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

    // OBS 는 별도 프로세스(작업 디렉터리 = OBS bin) 이므로 상대경로를
    // 앱 디렉터리 기준 절대경로로 변환해 넘겨야 한다(qt 경로와 동일 기준).
    QString mediaPath = L.media;
    if (QDir::isRelativePath(mediaPath))
        mediaPath = QDir(QCoreApplication::applicationDirPath())
                        .absoluteFilePath(mediaPath);
    if (!QFileInfo::exists(mediaPath))
        qWarning() << "ObsLiveBackend: media not found —" << mediaPath
                   << "(layer" << L.id << ")";

    QString kind;
    QJsonObject settings;
    if (L.mediaType == MediaType::Video) {
        kind = QStringLiteral("ffmpeg_source");
        settings["local_file"]          = mediaPath;
        settings["is_local_file"]       = true;
        settings["looping"]             = (L.endAction == EndAction::Loop);
        settings["restart_on_activate"] = true;
        settings["hw_decode"]           = false;  // HW 디코드 실패 시 검은 화면 방지(우선 SW)
    } else if (L.mediaType == MediaType::Image) {
        kind = QStringLiteral("image_source");
        settings["file"] = mediaPath;
    } else {
        qWarning() << "ObsLiveBackend: skipping unsupported layer"
                   << L.id << "(type" << static_cast<int>(L.mediaType)
                   << "— Document/PPT OBS 연동은 후속)";
        next();
        return;
    }

    // 세션 단조 카운터로 매 생성마다 유니크한 이름 → "already exists"
    // 가 구조적으로 불가능. 세대 추적은 m_inputsByScene 으로, 이전 세대
    // 정리는 rebuildScene 이, 세션 간 잔존은 seed purge 가 담당.
    const QString inputName =
        QStringLiteral("uwp_%1").arg(++m_inputSeq);
    m_inputsByScene[scene] << inputName;

    qInfo() << "ObsLiveBackend: layer" << L.id << "→ input" << inputName
            << kind << "geom" << L.geometry << "file=" << mediaPath;

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
                           << L.id << "input" << inputName << "—" << cm;
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

    // 활성 scene transition 을 (kind 로 찾아둔) cut/fade 표시이름으로 설정 →
    // SetCurrentProgramScene 이 그 전환으로 program 을 target 으로 바꾼다.
    // Studio Mode/TriggerStudioModeTransition 의존 제거(이 환경서 어긋남).
    const QString tname = m_fade ? m_fadeName : m_cutName;

    auto applyProgram = [this, targetScene]() {
        ObsClient* cp = client();
        if (!cp) return;
        m_inFlightCommit = m_pendingCommit;
        m_programScene   = targetScene;
        QJsonObject ps; ps["sceneName"] = targetScene;
        cp->request(QStringLiteral("SetCurrentProgramScene"), ps,
            [this, targetScene](bool ok, const QJsonObject&,
                                const QString& cm) {
                if (!ok)
                    qWarning() << "ObsLiveBackend: SetCurrentProgramScene "
                                  "failed —" << cm;
                ObsClient* cg = client();
                if (cg)
                    cg->request(QStringLiteral("GetCurrentProgramScene"), {},
                        [targetScene](bool, const QJsonObject& d,
                                      const QString&) {
                            qInfo() << "ObsLiveBackend: program scene now ="
                                    << d.value("sceneName").toString()
                                    << d.value("currentProgramSceneName")
                                           .toString()
                                    << "(target" << targetScene << ")";
                        });
            });
        qInfo() << "ObsLiveBackend: transition →" << targetScene
                << (m_fade ? "(fade)" : "(cut)");
    };

    if (tname.isEmpty()) {       // 전환 못 찾음 → 전환 없이 즉시 전환
        applyProgram();
        return;
    }
    QJsonObject tn; tn["transitionName"] = tname;
    c->request(QStringLiteral("SetCurrentSceneTransition"), tn,
        [this, applyProgram](bool ok, const QJsonObject&,
                             const QString& cm) {
            if (!ok)
                qWarning() << "ObsLiveBackend: SetCurrentSceneTransition "
                              "failed —" << cm;
            ObsClient* c2 = client();
            if (!c2) { applyProgram(); return; }
            if (m_fade) {
                QJsonObject td; td["transitionDuration"] = m_fadeMs;
                c2->request(
                    QStringLiteral("SetCurrentSceneTransitionDuration"), td,
                    [applyProgram](bool, const QJsonObject&,
                                   const QString&) { applyProgram(); });
            } else {
                applyProgram();
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
