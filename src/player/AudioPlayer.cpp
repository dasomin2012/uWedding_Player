#include "AudioPlayer.h"

#include "VlcInstance.h"

#ifdef UWP_HAS_VLC
  #include <vlc/vlc.h>
#endif

#include <QDebug>
#include <QFileInfo>
#include <QUrl>

namespace uwp {

AudioPlayer::AudioPlayer(QObject* parent)
    : QObject(parent)
{
#ifdef UWP_HAS_VLC
    auto* vlc = static_cast<libvlc_instance_t*>(
        VlcInstance::instance().nativeHandle());
    if (!vlc) {
        qWarning() << "AudioPlayer: libVLC 인스턴스 없음";
        return;
    }
    auto* p = libvlc_media_player_new(vlc);
    if (!p) {
        qWarning() << "AudioPlayer: libvlc_media_player_new 실패";
        return;
    }
    // 비디오 출력 억제 — HWND 미설정 시 libVLC 는 dummy 비디오 출력 사용.
    //   오디오는 정상 재생. (오디오 온리 미디어라면 이 호출도 무해.)
    libvlc_media_player_set_hwnd(p, nullptr);
    m_player = p;
#endif
}

AudioPlayer::~AudioPlayer() {
    stop();
#ifdef UWP_HAS_VLC
    if (m_player) {
        libvlc_media_player_release(static_cast<libvlc_media_player_t*>(m_player));
        m_player = nullptr;
    }
#endif
}

bool AudioPlayer::isPlaying() const {
#ifdef UWP_HAS_VLC
    if (!m_player) return false;
    return libvlc_media_player_is_playing(
        static_cast<libvlc_media_player_t*>(m_player)) != 0;
#else
    return false;
#endif
}

void AudioPlayer::play(const QString& path, int volume, bool loop) {
#ifdef UWP_HAS_VLC
    if (!m_player) return;
    if (path.isEmpty()) { stop(); return; }
    if (!QFileInfo::exists(path)) {
        qWarning() << "AudioPlayer::play: 파일 없음" << path;
        return;
    }

    // 동일 파일 재재생 요청 — 이미 재생중이면 볼륨/loop 만 갱신하고 유지.
    auto* p = static_cast<libvlc_media_player_t*>(m_player);
    if (path == m_currentPath && isPlaying()) {
        setVolume(volume);
        applyLoop(loop);
        return;
    }

    libvlc_media_player_stop(p);

    auto* vlc = static_cast<libvlc_instance_t*>(
        VlcInstance::instance().nativeHandle());
    const QByteArray url = QUrl::fromLocalFile(path).toEncoded();
    auto* media = libvlc_media_new_location(vlc, url.constData());
    if (!media) {
        qWarning() << "AudioPlayer::play: media_new 실패" << path;
        return;
    }
    if (loop) libvlc_media_add_option(media, "input-repeat=65535");
    libvlc_media_player_set_media(p, media);
    libvlc_media_release(media);

    m_currentPath = path;
    m_loop        = loop;
    setVolume(volume);

    const int rc = libvlc_media_player_play(p);
    if (rc != 0) {
        qWarning() << "AudioPlayer::play: play 실패 rc=" << rc;
    } else {
        qInfo() << "AudioPlayer: play" << path
                << "vol=" << volume << "loop=" << loop;
    }
#else
    Q_UNUSED(path); Q_UNUSED(volume); Q_UNUSED(loop);
#endif
}

void AudioPlayer::stop() {
#ifdef UWP_HAS_VLC
    if (!m_player) return;
    auto* p = static_cast<libvlc_media_player_t*>(m_player);
    if (libvlc_media_player_is_playing(p))
        libvlc_media_player_stop(p);
    m_currentPath.clear();
#endif
}

void AudioPlayer::setVolume(int volume) {
    m_volume = qBound(0, volume, 100);
#ifdef UWP_HAS_VLC
    if (m_player)
        libvlc_audio_set_volume(
            static_cast<libvlc_media_player_t*>(m_player), m_volume);
#endif
}

void AudioPlayer::applyLoop(bool loop) {
    // libVLC 는 재생 중 loop 옵션 실시간 변경이 제한적이라 media 재설정 없이
    //   플레이어 레벨에서만 관리. loop 값은 다음 play 시 재적용.
    m_loop = loop;
}

QList<AudioPlayer::OutputDevice> AudioPlayer::availableOutputs() const {
    QList<OutputDevice> devices;
#ifdef UWP_HAS_VLC
    if (!m_player) return devices;
    auto* p = static_cast<libvlc_media_player_t*>(m_player);
    libvlc_audio_output_device_t* head =
        libvlc_audio_output_device_enum(p);
    for (auto* it = head; it != nullptr; it = it->p_next) {
        const QString id   = it->psz_device      ? QString::fromUtf8(it->psz_device)      : QString();
        const QString desc = it->psz_description ? QString::fromUtf8(it->psz_description) : id;
        devices.append({ id, desc });
    }
    if (head) libvlc_audio_output_device_list_release(head);
#endif
    return devices;
}

QString AudioPlayer::currentOutput() const {
    return m_outputDev;
}

void AudioPlayer::setOutput(const QString& deviceId) {
    m_outputDev = deviceId;
#ifdef UWP_HAS_VLC
    if (!m_player) return;
    auto* p = static_cast<libvlc_media_player_t*>(m_player);
    // module 인자 nullptr → 활성 audio output 모듈에 device 지정.
    const QByteArray dev = deviceId.toUtf8();
    libvlc_audio_output_device_set(p, nullptr,
                                    deviceId.isEmpty() ? nullptr : dev.constData());
    qInfo() << "AudioPlayer: output device =" << (deviceId.isEmpty() ? "(default)" : deviceId);
#endif
}

} // namespace uwp
