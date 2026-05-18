#include "VideoWidget.h"

#ifdef UWP_HAS_VLC
  #include <vlc/vlc.h>
#endif

#include <QDebug>
#include <QFileInfo>
#include <QPalette>

namespace uwp {

struct VideoWidget::Impl {
#ifdef UWP_HAS_VLC
    libvlc_instance_t*     vlc    = nullptr;
    libvlc_media_player_t* player = nullptr;
#endif
    bool    playing     = false;
    QString currentPath;
};

VideoWidget::VideoWidget(QWidget* parent)
    : QWidget(parent)
    , m_impl(std::make_unique<Impl>())
{
    // libVLC 가 HWND 에 직접 그리도록 Qt 의 paint 를 비활성화하고
    // 별도 네이티브 윈도우를 갖게 한다.
    setAttribute(Qt::WA_NativeWindow);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_NoSystemBackground);

    QPalette pal = palette();
    pal.setColor(QPalette::Window, Qt::black);
    setPalette(pal);
    setAutoFillBackground(false);

#ifdef UWP_HAS_VLC
    const char* args[] = {
        "--quiet",
        "--no-video-title-show",
        "--avcodec-hw=any",
    };
    m_impl->vlc = libvlc_new(static_cast<int>(sizeof(args) / sizeof(args[0])), args);
    if (!m_impl->vlc) {
        qWarning() << "VideoWidget: libvlc_new failed";
        return;
    }
    m_impl->player = libvlc_media_player_new(m_impl->vlc);
    if (!m_impl->player) {
        qWarning() << "VideoWidget: libvlc_media_player_new failed";
    }
#else
    qInfo() << "VideoWidget: built without libVLC (stub mode)";
#endif
}

VideoWidget::~VideoWidget() {
#ifdef UWP_HAS_VLC
    if (m_impl->player) {
        libvlc_media_player_stop(m_impl->player);
        libvlc_media_player_release(m_impl->player);
        m_impl->player = nullptr;
    }
    if (m_impl->vlc) {
        libvlc_release(m_impl->vlc);
        m_impl->vlc = nullptr;
    }
#endif
}

bool VideoWidget::play(const QString& path) {
#ifdef UWP_HAS_VLC
    if (!m_impl->player) {
        qWarning() << "VideoWidget::play: VLC not initialized";
        return false;
    }
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        qWarning() << "VideoWidget::play: media not found -" << path;
        return false;
    }

    libvlc_media_t* media = libvlc_media_new_path(m_impl->vlc, path.toUtf8().constData());
    if (!media) {
        qWarning() << "VideoWidget::play: libvlc_media_new_path failed";
        return false;
    }
    // Phase 1 테스트용 무한 루프
    libvlc_media_add_option(media, ":input-repeat=65535");

    libvlc_media_player_set_media(m_impl->player, media);
    libvlc_media_release(media);

    // HWND 바인딩은 winId() 호출 시점에 네이티브 윈도우가 보장됨 (WA_NativeWindow).
    libvlc_media_player_set_hwnd(m_impl->player, reinterpret_cast<void*>(winId()));

    const int rc = libvlc_media_player_play(m_impl->player);
    m_impl->playing     = (rc == 0);
    m_impl->currentPath = path;
    if (!m_impl->playing) {
        qWarning() << "VideoWidget::play: libvlc_media_player_play failed";
    } else {
        qInfo() << "VideoWidget::play:" << path;
    }
    return m_impl->playing;
#else
    Q_UNUSED(path);
    qInfo() << "VideoWidget::play (stub):" << path << "- libVLC not linked";
    return false;
#endif
}

void VideoWidget::stop() {
#ifdef UWP_HAS_VLC
    if (m_impl->player) {
        libvlc_media_player_stop(m_impl->player);
    }
#endif
    m_impl->playing = false;
}

bool VideoWidget::isPlaying() const {
    return m_impl->playing;
}

} // namespace uwp
