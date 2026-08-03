#pragma once

#include <QWidget>
#include <QString>

#include "scene/Layer.h"   // EndAction

class QLineEdit;
class QComboBox;
class QPushButton;
class QSlider;
class QCheckBox;
class QLabel;

namespace uwp {

struct Program;

// 편집중 프로그램의 속성 편집 위젯 (우측 패널).
//   - 이름 (QLineEdit)
//   - 종료 동작 (QComboBox: Loop/Stop/Hold/Next/First)
// 프로그램 단위 표시 시간은 Phase A 에서 첫 페이지 시간과 동일 개념이라
// PageProperties 로 이관 — 여기선 프로그램만의 필드에 집중.
class ProgramProperties : public QWidget {
    Q_OBJECT
public:
    explicit ProgramProperties(QWidget* parent = nullptr);

    // programIndex/programCount 는 앞/뒤로 버튼 활성 판단용 (경계 도달 시 비활성).
    void setProgram(const Program* program,
                    int programIndex = -1, int programCount = 0);

    // 사용자가 오디오 출력 장치를 선택할 수 있게 후보 목록 주입 (앱 시작 후
    //   AudioPlayer 로부터 조회한 결과). 빈 리스트면 버튼 비활성.
    void setAudioOutputs(const QList<QPair<QString, QString>>& devices,
                         const QString& currentId);

signals:
    void renameRequested(const QString& newName);
    void endActionChanged(EndAction action);
    void moveUpRequested();     // 앞으로 (index 감소)
    void moveDownRequested();   // 뒤로 (index 증가)
    // BGM — 프로그램 단위 배경음.
    void bgmPathChanged(const QString& path);
    void bgmVolumeChanged(int volume);   // 0..100
    void bgmLoopChanged(bool loop);
    // 오디오 출력 장치(전역) 선택 — 앱 세션 지속.
    void audioOutputChanged(const QString& deviceId);

private slots:
    void onAddBgm();               // "+ 추가" — 파일 선택 다이얼로그
    void onBgmMenu();              // 파일명 우클릭/드롭다운 → 제거

private:
    void showOutputMenu();          // 볼륨 우측 스피커 버튼 → 장치 메뉴

    QLineEdit*   m_name        = nullptr;
    QComboBox*   m_endAction   = nullptr;
    QPushButton* m_btnUp       = nullptr;
    QPushButton* m_btnDown     = nullptr;
    // BGM 위젯 —
    QLabel*      m_bgmPathLbl  = nullptr;   // 파일명만 표시 (경로 절단)
    QPushButton* m_bgmAdd      = nullptr;   // "+ 추가" — 파일 선택
    QPushButton* m_bgmClear    = nullptr;   // 파일 설정 시에만 노출되는 작은 X
    QSlider*     m_bgmVolume   = nullptr;
    QLabel*      m_bgmVolLbl   = nullptr;
    QCheckBox*   m_bgmLoop     = nullptr;
    QPushButton* m_bgmOutput   = nullptr;   // 오디오 출력 장치 선택 버튼
    QList<QPair<QString, QString>> m_outputs;   // <id, description>
    QString      m_outputCurrent;
    bool         m_loading     = false;
};

} // namespace uwp
