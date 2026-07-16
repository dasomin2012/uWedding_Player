#pragma once

#include <QObject>
#include <QVector>
#include <QString>
#include <QStringList>
#include <QHash>
#include <QJsonObject>
#include <functional>

#include "live/ILiveSink.h"
#include "scene/Layer.h"
#include "app/Settings.h"   // ObsConfig

namespace uwp {

class ObsClient;
class ObsProcessManager;

// engine=obs 송출 백엔드. ILiveSink 구현.
//
// ObsProcessManager 가 띄운 OBS 를 obs-websocket 으로 제어한다(libobs 무관).
// Studio Mode + 두 씬(PGM_A/PGM_B) 핑퐁으로 stagger 없는 원자 전환을
// OBS 컴포지터에 위임한다. OBS 캔버스 = 우리 논리 캔버스이므로 레이어
// geometry 는 1:1 매핑(LiveWindow 의 fit 계산 불필요).
//
// 스코프: 영상(ffmpeg_source)·이미지(image_source) 네이티브. opacity/전환
// 종류는 best-effort. Document/PPT 는 경고 후 skip(스냅샷 연동은 후속).
class ObsLiveBackend : public QObject, public ILiveSink {
    Q_OBJECT
public:
    ObsLiveBackend(ObsProcessManager* proc, const ObsConfig& cfg,
                   QObject* parent = nullptr);
    ~ObsLiveBackend() override;

    // ----- ILiveSink -----
    void setCanvasSize(int width, int height) override;
    void showOnMonitor(int monitorIndex) override;   // 프로젝터는 ProcessManager 소관
    void setTransition(bool fade, int fadeDurationMs) override;
    void applyScene(const QVector<Layer>& layers,
                    std::function<void()> onCommitted = {}) override;
    QWidget* transitionAnchor() override { return nullptr; }  // 비위젯

    // Live 미러 스냅샷 — obs-websocket GetSourceScreenshot 로 현재 program 씬을
    // base64 PNG 로 받아 QImage 로 디코드 (비동기).
    // 미준비/미시딩 상태에서는 QImage() 로 콜백.
    void requestMirrorSnapshot(int maxWidthPx, MirrorCallback cb) override;

signals:
    void transitionEnded();   // O5 NovaStar 동기 지점

private slots:
    void onObsReady();
    void onObsEvent(const QString& eventType, const QJsonObject& eventData);

private:
    ObsClient* client() const;
    void seed();
    void doApply();
    void rebuildScene(const QString& scene, const QVector<Layer>& layers,
                      std::function<void()> done);
    void buildLayer(const QString& scene, QVector<Layer> layers, int i,
                    std::function<void()> done);
    void triggerTransition(const QString& targetScene);

    ObsProcessManager* m_proc        = nullptr;
    bool               m_clientBound = false;
    bool               m_seeded      = false;

    int  m_canvasW = 1920;
    int  m_canvasH = 1080;
    bool m_fade    = true;
    int  m_fadeMs  = 800;

    QString m_sceneA;
    QString m_sceneB;
    QString m_cutName;     // 로케일별 Cut 전환 표시이름 (kind 로 탐색)
    QString m_fadeName;    // 로케일별 Fade 전환 표시이름
    QString m_programScene;                        // 현재 송출 씬 ("" = 미정)
    QHash<QString, QStringList> m_inputsByScene;   // 우리가 만든 input 추적
    quint64 m_inputSeq = 0;                        // 세션 유니크 input 이름용

    bool                  m_havePending = false;
    QVector<Layer>        m_pendingLayers;
    std::function<void()> m_pendingCommit;
    std::function<void()> m_inFlightCommit;        // 전환 트리거 후 End 대기
};

} // namespace uwp
