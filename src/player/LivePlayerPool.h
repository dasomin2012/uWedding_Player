#pragma once

#include <QString>
#include <memory>
#include <vector>

namespace uwp {

// 단일 libvlc_media_player_t 래퍼. 공유 VlcInstance 위에서 생성된다.
// 미디어 경로는 QUrl 로 file:// URI 변환 후 libvlc_media_new_location 사용
// (Windows 절대경로/공백/유니코드 안전).
class LivePlayer {
public:
    explicit LivePlayer(void* vlcInstance);   // libvlc_instance_t*
    ~LivePlayer();

    bool valid() const;

    void setHwnd(void* hwnd);                 // 출력 대상 네이티브 윈도우
    bool play(const QString& path, bool loop = true);
    void stop();
    void pause();                             // 토글
    bool isPlaying() const;
    bool hasVideoOutput() const;              // vout 모듈 생성 여부(약한 신호)
    bool isPrimed() const;                    // 실제 첫 프레임 렌더 완료(강한 신호)
    void freeze();                            // 첫 프레임에서 일시정지(cue)
    void resume();                            // 재개

    LivePlayer(const LivePlayer&)            = delete;
    LivePlayer& operator=(const LivePlayer&) = delete;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// LivePlayer 들을 풀링한다. 반환된 플레이어는 정지 후 idle 로 재사용.
// Phase 2: LiveWindow 가 1개 acquire. Phase 3+: 레이어 수만큼 확장.
class LivePlayerPool {
public:
    LivePlayerPool();
    ~LivePlayerPool();

    LivePlayer* acquire();             // idle 재사용 또는 신규 생성
    void        release(LivePlayer*);  // 정지 후 idle 로 반환

    int totalCount()  const { return static_cast<int>(m_all.size()); }
    int idleCount()   const { return static_cast<int>(m_idle.size()); }
    int activeCount() const { return totalCount() - idleCount(); }

    LivePlayerPool(const LivePlayerPool&)            = delete;
    LivePlayerPool& operator=(const LivePlayerPool&) = delete;

private:
    void*                                    m_vlc = nullptr;  // libvlc_instance_t*
    std::vector<std::unique_ptr<LivePlayer>> m_all;
    std::vector<LivePlayer*>                 m_idle;
};

} // namespace uwp
