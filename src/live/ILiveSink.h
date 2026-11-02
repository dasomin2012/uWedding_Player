#pragma once

#include <QImage>
#include <QString>
#include <QVector>
#include <functional>

#include "scene/Layer.h"

class QWidget;

namespace uwp {

// Live 재생 진행 상태 (ControlWindow 재생 바용). valid=false 면 영상 없음.
struct PlaybackStatus {
    bool    valid      = false;
    bool    playing    = false;   // 실제 진행 중(일시정지/정지면 false)
    qint64  positionMs = 0;
    qint64  lengthMs   = 0;       // 0 = 아직 길이 모름
    QString label;                // 대표 영상 이름(레이어명 또는 파일명)
};

// 여러 영상 레이어 중 진행 바에 표시할 "대표 영상" — 면적 최대, 동률이면
// zIndex 가 높은 쪽. 영상이 없으면 -1. 두 백엔드가 같은 규칙을 쓰도록 공유.
inline int primaryVideoIndex(const QVector<Layer>& layers) {
    int best = -1;
    double bestArea = -1.0;
    for (int i = 0; i < layers.size(); ++i) {
        const Layer& l = layers[i];
        if (l.mediaType != MediaType::Video) continue;
        const double area = l.geometry.width() * l.geometry.height();
        if (best < 0 || area > bestArea
            || (area == bestArea && l.zIndex > layers[best].zIndex)) {
            best = i;
            bestArea = area;
        }
    }
    return best;
}

inline QString playbackLabel(const Layer& l) {
    if (!l.name.isEmpty()) return l.name;
    const int slash = qMax(l.media.lastIndexOf('/'), l.media.lastIndexOf('\\'));
    return slash >= 0 ? l.media.mid(slash + 1) : l.media;
}

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

    // Black: 송출은 유지한 채 화면만 검정으로 (페이지 상태 보존).
    //   다음 applyScene(TAKE) 이전에 호출자가 해제한다.
    virtual void setBlack(bool /*black*/) {}

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

    // Live 대표 영상의 재생 위치/길이 요청 (표시 전용, seek 없음).
    // qt 백엔드: 동기 (libVLC get_time/get_length),
    // obs 백엔드: 비동기 (websocket GetMediaInputStatus).
    // 미지원/영상 없음이면 valid=false 로 1회 콜백.
    using PlaybackCallback = std::function<void(const PlaybackStatus&)>;
    virtual void requestPlaybackStatus(PlaybackCallback cb) {
        if (cb) cb(PlaybackStatus{});
    }
};

} // namespace uwp
