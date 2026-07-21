#pragma once

#include <QDialog>
#include <QRect>
#include <QString>

class QComboBox;
class QGroupBox;
class QLabel;
class QRadioButton;
class QSpinBox;

namespace uwp {

// LED 웨딩홀 현장 세팅 다이얼로그 — 두 가지 출력 방식을 라디오로 분리.
//
//   1) 모니터 모드 ("monitor")
//      선택한 모니터의 전체 화면을 채워서 송출. 모니터 콤보로 인덱스 지정.
//   2) 스크린 모드 ("screen")
//      X/Y/W/H 를 직접 지정 — 데스크톱 가상 좌표계에서 정확한 영역에 송출.
//      LED 스크린이 특정 모니터의 부분 영역이거나 여러 모니터에 걸친 경우용.
//
// Application 이 accepted() 후 mode()/monitorIndex()/geometry() 로 값 취해
// 실제 반영을 담당(엔진별 분기).
class DisplaySettingsDialog : public QDialog {
    Q_OBJECT
public:
    DisplaySettingsDialog(QWidget* parent,
                          const QString& currentMode,     // "monitor" | "screen"
                          int currentMonitorIndex,
                          const QRect& currentGeometry);  // screen 모드용 X/Y/W/H

    QString mode()             const;   // "monitor" | "screen"
    int     monitorIndex()     const;
    QRect   outputGeometry()   const;   // 스크린 모드 값. 모니터 모드에서는
                                        // 선택 모니터의 실제 geometry 로 채워 반환.
                                        // (QWidget::geometry 와 이름 충돌 회피)

private:
    void   updateEnabledStates();
    void   populateMonitors(int currentIdx);

    QRadioButton* m_radioMonitor = nullptr;
    QRadioButton* m_radioScreen  = nullptr;
    QComboBox*    m_monitor      = nullptr;
    QSpinBox*     m_x            = nullptr;
    QSpinBox*     m_y            = nullptr;
    QSpinBox*     m_w            = nullptr;
    QSpinBox*     m_h            = nullptr;
    QLabel*       m_hint         = nullptr;
};

} // namespace uwp
