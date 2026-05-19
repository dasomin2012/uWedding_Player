#include "VlcInstance.h"

#ifdef UWP_HAS_VLC
  #include <vlc/vlc.h>
#endif

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

namespace uwp {

VlcInstance& VlcInstance::instance() {
    static VlcInstance s_instance;
    return s_instance;
}

VlcInstance::VlcInstance() {
#ifdef UWP_HAS_VLC
    // 플러그인 경로: 실행 파일 옆 plugins/ (CMake post-build 로 복사됨).
    // 이미 VLC_PLUGIN_PATH 가 설정돼 있으면 존중.
    if (qEnvironmentVariableIsEmpty("VLC_PLUGIN_PATH")) {
        const QString pluginDir =
            QDir(QCoreApplication::applicationDirPath()).filePath("plugins");
        if (QFileInfo::exists(pluginDir)) {
            qputenv("VLC_PLUGIN_PATH", QDir::toNativeSeparators(pluginDir).toUtf8());
            qInfo() << "VlcInstance: VLC_PLUGIN_PATH =" << pluginDir;
        } else {
            qWarning() << "VlcInstance: plugins dir not found at" << pluginDir
                       << "- libVLC may fail to find codecs";
        }
    }

    // uPlayer_win 검증 조합: 다중 플레이어 동시 시작 안정 + Qt/오버레이 합성 호환.
    //  --avcodec-hw=none : HW 디코더 경합 제거(레이어 "한 개씩" 현상 완화)
    //  --vout=direct2d   : Windows 에서 Qt 합성과 호환 좋은 vout
    //  --no-overlay      : 하드웨어 오버레이 비활성(합성 간섭 방지)
    const char* args[] = {
        "--quiet",
        "--no-video-title-show",
        "--avcodec-hw=none",
        "--vout=direct2d",
        "--no-overlay",
    };
    auto* vlc = libvlc_new(static_cast<int>(sizeof(args) / sizeof(args[0])), args);
    if (!vlc) {
        qWarning() << "VlcInstance: libvlc_new failed";
    } else {
        qInfo() << "VlcInstance: libVLC initialized";
    }
    m_handle = vlc;
#else
    qInfo() << "VlcInstance: built without libVLC (stub mode)";
#endif
}

VlcInstance::~VlcInstance() {
#ifdef UWP_HAS_VLC
    if (m_handle) {
        libvlc_release(static_cast<libvlc_instance_t*>(m_handle));
        m_handle = nullptr;
    }
#endif
}

} // namespace uwp
