#pragma once

#include <QHash>
#include <QObject>
#include <QString>

class QTimer;

namespace uwp {

// 동영상 파일 길이(ms) 조회 — libVLC 로컬 파싱(재생 없이 메타데이터만).
//   duration(path): 캐시에 있으면 값, 분석 중/미요청이면 0(분석 시작), 실패 -1.
//   분석 완료 시 durationReady 발화. 파싱은 libVLC 내부 스레드에서 비동기로
//   진행되고, 이 객체는 GUI 스레드 타이머로 완료 여부만 확인한다.
// libVLC SDK 미탑재(UWP_HAS_VLC 미정의) 시 항상 -1.
class MediaDurationProbe : public QObject {
    Q_OBJECT
public:
    explicit MediaDurationProbe(QObject* parent = nullptr);
    ~MediaDurationProbe() override;

    qint64 duration(const QString& path);

signals:
    void durationReady(const QString& path, qint64 ms);

private:
    void poll();

    QHash<QString, qint64> m_cache;     // path → ms (-1 = 실패)
    QHash<QString, void*>  m_pending;   // path → libvlc_media_t*
    QHash<QString, int>    m_ticks;     // path → 경과 폴링 횟수 (타임아웃)
    QTimer*                m_timer = nullptr;
};

} // namespace uwp
