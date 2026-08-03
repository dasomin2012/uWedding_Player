#pragma once

#include <QObject>
#include <QList>
#include <QPair>
#include <QString>

namespace uwp {

// BGM 재생 전용 libVLC 래퍼.  Video widget 매핑 없이 순수 오디오 출력.
//  프로그램 단위 BGM 을 담당: TAKE 로 프로그램이 Live 진입할 때 play(),
//  다른 프로그램으로 스위치되거나 Live 정지 시 stop().
//  UWP_HAS_VLC 미정의 시 no-op 스텁으로 안전 동작.
class AudioPlayer : public QObject {
    Q_OBJECT
public:
    // 오디오 출력 장치 <내부 id, 사용자 표시명>.
    using OutputDevice = QPair<QString, QString>;

    explicit AudioPlayer(QObject* parent = nullptr);
    ~AudioPlayer() override;

    // path 는 로컬 파일 절대경로. loop 은 재생 종료 시 자동 재시작.
    // volume 은 0..100. 재생 중이면 새 미디어로 즉시 교체.
    void play(const QString& path, int volume, bool loop);
    void stop();
    void setVolume(int volume);   // 0..100

    QString currentPath() const { return m_currentPath; }
    bool    isPlaying()   const;

    // ---- 출력 장치 관리 ----
    QList<OutputDevice> availableOutputs() const;   // 시스템 오디오 장치 목록
    QString             currentOutput()    const;   // 현재 선택된 장치 id
    void                setOutput(const QString& deviceId);  // id="" 이면 기본

private:
    void   applyLoop(bool loop);

    void*    m_player     = nullptr;   // libvlc_media_player_t*
    QString  m_currentPath;
    bool     m_loop       = false;
    int      m_volume     = 80;
    QString  m_outputDev;              // libVLC 장치 id (빈값 = system default)
};

} // namespace uwp
