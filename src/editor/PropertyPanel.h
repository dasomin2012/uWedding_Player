#pragma once

#include <QWidget>
#include <QString>

class QLineEdit;
class QSpinBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QCheckBox;

namespace uwp {

class SceneModel;

// 선택된 Layer 의 속성을 양방향 편집 (단일 패널).
//   - "꽉 채우기" 버튼 + 위치/크기 직접 입력(왼쪽/위/가로/세로)
//   - "비율 고정" 체크 시 가로↔세로 입력이 종횡비를 유지하며 연동
//   - 투명도 슬라이더(%), 표시시간, 재생끝나면, 표시순서
//   * 레이어 삭제는 Preview 툴바(오른쪽 휴지통 아이콘)로 이동.
// 모델 selectionChanged/layerChanged 를 구독해 필드 갱신,
// 필드 편집 시 모델에 기록 (m_loading 가드로 에코/루프 방지).
class PropertyPanel : public QWidget {
    Q_OBJECT
public:
    PropertyPanel(SceneModel* model, QWidget* parent = nullptr);

private slots:
    void onSelectionChanged(const QString& id);
    void onLayerChanged(const QString& id);

    void onFillClicked();              // 캔버스 꽉 채우기
    void onWidthChanged(int w);        // 비율 고정 시 세로 자동
    void onHeightChanged(int h);       // 비율 고정 시 가로 자동
    void commitGeometry();             // 현 X/Y/W/H 를 모델에 반영
    void onOpacitySliderReleased();
    void commitDisplayTime(int v);
    void commitEndAction(int idx);
    void commitName();

private:
    void loadFrom(const QString& id);
    void setEnabledAll(bool on);

    SceneModel*  m_model   = nullptr;
    QString      m_id;
    bool         m_loading = false;

    QLabel*      m_media    = nullptr;
    QLineEdit*   m_name     = nullptr;

    // 위치/크기
    QPushButton* m_btnFill   = nullptr;   // 꽉 채우기
    QSpinBox*    m_x = nullptr;
    QSpinBox*    m_y = nullptr;
    QSpinBox*    m_w = nullptr;
    QSpinBox*    m_h = nullptr;
    QCheckBox*   m_lockAspect = nullptr;  // 비율 고정 (가로↔세로 연동)

    // 투명도
    QSlider*     m_opacitySlider  = nullptr;   // 0..100 (%)
    QLabel*      m_opacityReadout = nullptr;

    // 공통
    QSpinBox*    m_display   = nullptr;
    QComboBox*   m_endAction = nullptr;        // userData = EndAction
    QPushButton* m_btnFront  = nullptr;
    QPushButton* m_btnRaise  = nullptr;
    QPushButton* m_btnLower  = nullptr;
    QPushButton* m_btnBack   = nullptr;
};

} // namespace uwp
