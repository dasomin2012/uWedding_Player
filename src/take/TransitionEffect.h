#pragma once

#include <QObject>
#include <functional>

class QWidget;

namespace uwp {

// Take 전환 효과.
//  - Cut : apply() 즉시 호출
//  - Fade: dip-to-black. 출력 화면을 덮는 top-level 검은 창의
//          windowOpacity 0→1(절반)→apply()→1→0(절반) 애니메이션.
//          libVLC 네이티브 HWND 영상과도 DWM 합성으로 동작.
class TransitionEffect : public QObject {
    Q_OBJECT
public:
    enum class Mode { Cut, Fade };

    explicit TransitionEffect(QObject* parent = nullptr);

    // liveWindow: 출력 윈도우(화면 지오메트리 기준으로 오버레이 배치)
    // applyAsync(done): 새 씬 스테이징을 시작하고, commit 이 끝나면 done() 호출.
    //   - Cut : applyAsync 즉시 호출, done 은 무시(옛 씬 유지→준비완료 시 원자 스왑)
    //   - Fade: fadeIn → (검은 화면) applyAsync → done 수신 시 fadeOut
    void run(Mode mode, int durationMs, QWidget* liveWindow,
             std::function<void(std::function<void()>)> applyAsync);

    bool isRunning() const { return m_running; }

private:
    bool m_running = false;
};

} // namespace uwp
