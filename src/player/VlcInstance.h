#pragma once

#include <QString>

namespace uwp {

// 프로세스 전역에서 단 하나의 libvlc_instance_t 를 보유한다.
// 플러그인 경로 설정 후 libvlc_new 를 1회만 호출 → 플레이어들이 공유.
// libVLC SDK 미탑재(UWP_HAS_VLC 미정의) 시 nativeHandle() 은 nullptr.
class VlcInstance {
public:
    static VlcInstance& instance();

    // libvlc_instance_t* 를 void* 로 노출 (헤더를 VLC 타입에서 자유롭게 유지).
    // 사용처에서 static_cast<libvlc_instance_t*> 로 캐스팅.
    void* nativeHandle() const { return m_handle; }
    bool  isAvailable()  const { return m_handle != nullptr; }

    VlcInstance(const VlcInstance&)            = delete;
    VlcInstance& operator=(const VlcInstance&) = delete;

private:
    VlcInstance();
    ~VlcInstance();

    void* m_handle = nullptr;  // libvlc_instance_t*
};

} // namespace uwp
