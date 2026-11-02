#include "MediaDurationProbe.h"

#include "VlcInstance.h"

#ifdef UWP_HAS_VLC
  #include <vlc/vlc.h>
#endif

#include <QDebug>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>

namespace uwp {

namespace {
constexpr int kPollMs      = 100;
constexpr int kTimeoutTick = 60;   // 6초 — 파싱 타임아웃(5초)보다 약간 길게
}

MediaDurationProbe::MediaDurationProbe(QObject* parent)
    : QObject(parent)
{
    m_timer = new QTimer(this);
    m_timer->setInterval(kPollMs);
    connect(m_timer, &QTimer::timeout, this, &MediaDurationProbe::poll);
}

MediaDurationProbe::~MediaDurationProbe() {
#ifdef UWP_HAS_VLC
    for (void* m : qAsConst(m_pending)) {
        auto* media = static_cast<libvlc_media_t*>(m);
        libvlc_media_parse_stop(media);
        libvlc_media_release(media);
    }
#endif
}

qint64 MediaDurationProbe::duration(const QString& path) {
    if (path.isEmpty()) return -1;
    const auto it = m_cache.constFind(path);
    if (it != m_cache.constEnd()) return it.value();
    if (m_pending.contains(path)) return 0;
#ifdef UWP_HAS_VLC
    auto* vlc = static_cast<libvlc_instance_t*>(VlcInstance::instance().nativeHandle());
    if (!vlc || !QFileInfo::exists(path)) { m_cache.insert(path, -1); return -1; }
    const QByteArray mrl = QUrl::fromLocalFile(path).toEncoded();
    libvlc_media_t* media = libvlc_media_new_location(vlc, mrl.constData());
    if (!media) { m_cache.insert(path, -1); return -1; }
    if (libvlc_media_parse_with_options(media, libvlc_media_parse_local, 5000) != 0) {
        libvlc_media_release(media);
        m_cache.insert(path, -1);
        return -1;
    }
    m_pending.insert(path, media);
    m_ticks.insert(path, 0);
    if (!m_timer->isActive()) m_timer->start();
    return 0;
#else
    m_cache.insert(path, -1);
    return -1;
#endif
}

void MediaDurationProbe::poll() {
#ifdef UWP_HAS_VLC
    const QStringList paths = m_pending.keys();
    for (const QString& path : paths) {
        auto* media = static_cast<libvlc_media_t*>(m_pending.value(path));
        const libvlc_media_parsed_status_t st = libvlc_media_get_parsed_status(media);
        const int ticks = ++m_ticks[path];
        qint64 ms = 0;
        if (st == libvlc_media_parsed_status_done) {
            ms = libvlc_media_get_duration(media);
            if (ms <= 0) ms = -1;
        } else if (st == libvlc_media_parsed_status_failed
                   || st == libvlc_media_parsed_status_timeout
                   || st == libvlc_media_parsed_status_skipped
                   || ticks >= kTimeoutTick) {
            ms = -1;
        } else {
            continue;   // 아직 분석 중
        }
        if (st != libvlc_media_parsed_status_done) libvlc_media_parse_stop(media);
        libvlc_media_release(media);
        m_pending.remove(path);
        m_ticks.remove(path);
        m_cache.insert(path, ms);
        if (ms < 0) qWarning() << "MediaDurationProbe: failed" << path << "status" << st;
        emit durationReady(path, ms);
    }
#endif
    if (m_pending.isEmpty()) m_timer->stop();
}

} // namespace uwp
