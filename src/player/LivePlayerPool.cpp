#include "LivePlayerPool.h"

#include "VlcInstance.h"

#ifdef UWP_HAS_VLC
  #include <vlc/vlc.h>
#endif

#include <QDebug>
#include <QFileInfo>
#include <QUrl>
#include <algorithm>

namespace uwp {

// ============================================================
// LivePlayer
// ============================================================
struct LivePlayer::Impl {
#ifdef UWP_HAS_VLC
    libvlc_instance_t*     vlc    = nullptr;
    libvlc_media_player_t* player = nullptr;
#endif
    bool    playing = false;
    QString currentPath;
};

LivePlayer::LivePlayer(void* vlcInstance)
    : d(std::make_unique<Impl>())
{
#ifdef UWP_HAS_VLC
    d->vlc = static_cast<libvlc_instance_t*>(vlcInstance);
    if (d->vlc) {
        d->player = libvlc_media_player_new(d->vlc);
        if (!d->player) {
            qWarning() << "LivePlayer: libvlc_media_player_new failed";
        }
    }
#else
    Q_UNUSED(vlcInstance);
#endif
}

LivePlayer::~LivePlayer() {
#ifdef UWP_HAS_VLC
    if (d->player) {
        libvlc_media_player_stop(d->player);
        libvlc_media_player_release(d->player);
        d->player = nullptr;
    }
#endif
}

bool LivePlayer::valid() const {
#ifdef UWP_HAS_VLC
    return d->player != nullptr;
#else
    return false;
#endif
}

void LivePlayer::setHwnd(void* hwnd) {
#ifdef UWP_HAS_VLC
    if (d->player) {
        libvlc_media_player_set_hwnd(d->player, hwnd);
    }
#else
    Q_UNUSED(hwnd);
#endif
}

bool LivePlayer::play(const QString& path, bool loop) {
#ifdef UWP_HAS_VLC
    if (!d->player) {
        qWarning() << "LivePlayer::play: player not initialized";
        return false;
    }
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        qWarning() << "LivePlayer::play: media not found -" << path;
        return false;
    }

    const QByteArray mrl = QUrl::fromLocalFile(path).toEncoded();
    libvlc_media_t* media = libvlc_media_new_location(d->vlc, mrl.constData());
    if (!media) {
        const char* e = libvlc_errmsg();
        qWarning() << "LivePlayer::play: libvlc_media_new_location failed -"
                   << mrl << "-" << (e ? e : "(no libvlc error)");
        return false;
    }
    if (loop) {
        libvlc_media_add_option(media, ":input-repeat=65535");
    }

    libvlc_media_player_stop(d->player);
    libvlc_media_player_set_media(d->player, media);
    libvlc_media_release(media);

    const int rc = libvlc_media_player_play(d->player);
    d->playing     = (rc == 0);
    d->currentPath = path;
    if (!d->playing) {
        const char* e = libvlc_errmsg();
        qWarning() << "LivePlayer::play: libvlc_media_player_play failed -"
                   << (e ? e : "(no libvlc error)");
    } else {
        qInfo() << "LivePlayer::play:" << path;
    }
    return d->playing;
#else
    Q_UNUSED(path);
    Q_UNUSED(loop);
    qInfo() << "LivePlayer::play (stub):" << path;
    return false;
#endif
}

void LivePlayer::stop() {
#ifdef UWP_HAS_VLC
    if (d->player) {
        libvlc_media_player_stop(d->player);
    }
#endif
    d->playing = false;
}

void LivePlayer::pause() {
#ifdef UWP_HAS_VLC
    if (!d->player) return;
    const bool nowPaused = d->playing;
    libvlc_media_player_set_pause(d->player, nowPaused ? 1 : 0);
    d->playing = !nowPaused;
#else
    qInfo() << "LivePlayer::pause (stub)";
#endif
}

bool LivePlayer::isPlaying() const {
    return d->playing;
}

bool LivePlayer::hasVideoOutput() const {
#ifdef UWP_HAS_VLC
    if (!d->player) return true;   // 플레이어 없으면 대기시키지 않음
    return libvlc_media_player_has_vout(d->player) > 0;
#else
    return true;                   // stub: 파이프라인 정지 방지
#endif
}

bool LivePlayer::isPrimed() const {
#ifdef UWP_HAS_VLC
    if (!d->player) return true;   // 스텁/플레이어 없음 → 대기시키지 않음
    if (libvlc_media_player_has_vout(d->player) < 1) return false;
    if (libvlc_media_player_get_state(d->player) != libvlc_Playing) return false;
    // get_time()>0 == 실제로 첫 프레임을 표시하며 재생이 진행됨
    return libvlc_media_player_get_time(d->player) > 0;
#else
    return true;
#endif
}

void LivePlayer::freeze() {
#ifdef UWP_HAS_VLC
    if (d->player && libvlc_media_player_is_playing(d->player))
        libvlc_media_player_set_pause(d->player, 1);   // 현재 프레임에서 정지
#endif
}

void LivePlayer::resume() {
#ifdef UWP_HAS_VLC
    if (d->player)
        libvlc_media_player_set_pause(d->player, 0);   // 재개
#endif
    d->playing = true;
}

// ============================================================
// LivePlayerPool
// ============================================================
LivePlayerPool::LivePlayerPool() {
    m_vlc = VlcInstance::instance().nativeHandle();
    if (!m_vlc) {
        qWarning() << "LivePlayerPool: VlcInstance unavailable — "
                      "players will be invalid (stub/no SDK)";
    }
}

LivePlayerPool::~LivePlayerPool() = default;

LivePlayer* LivePlayerPool::acquire() {
    if (!m_idle.empty()) {
        LivePlayer* p = m_idle.back();
        m_idle.pop_back();
        return p;
    }
    auto up = std::make_unique<LivePlayer>(m_vlc);
    LivePlayer* p = up.get();
    m_all.push_back(std::move(up));
    qInfo() << "LivePlayerPool: created player, total =" << totalCount();
    return p;
}

void LivePlayerPool::release(LivePlayer* p) {
    if (!p) return;
    const bool owned =
        std::any_of(m_all.begin(), m_all.end(),
                    [p](const std::unique_ptr<LivePlayer>& u) { return u.get() == p; });
    if (!owned) {
        qWarning() << "LivePlayerPool::release: player not owned by this pool";
        return;
    }
    if (std::find(m_idle.begin(), m_idle.end(), p) != m_idle.end()) {
        return;  // 이미 반환됨
    }
    p->stop();
    m_idle.push_back(p);
}

} // namespace uwp
