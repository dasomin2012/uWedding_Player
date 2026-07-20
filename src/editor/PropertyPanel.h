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
//   - 위치/크기 직접 입력(왼쪽/위/가로/세로), 화면 비율(X:Y)
//   - "비율 고정" 체크 시 가로↔세로 입력이 종횡비를 유지하며 연동
//   - 투명도 슬라이더(%), 표시순서(z-order)
//   * 레이어 삭제 · 꽉 채우기는 Preview 툴바(오른쪽 아이콘)로 이동.
//   * 표시 시간 · 종료 동작은 Program 단위 개념으로 통일 — 프로그램 카드에서 편집.
// 모델 selectionChanged/layerChanged 를 구독해 필드 갱신,
// 필드 편집 시 모델에 기록 (m_loading 가드로 에코/루프 방지).
class PropertyPanel : public QWidget {
    Q_OBJECT
public:
    PropertyPanel(SceneModel* model, QWidget* parent = nullptr);

private slots:
    void onSelectionChanged(const QString& id);
    void onLayerChanged(const QString& id);

    void onWidthChanged(int w);        // 비율 고정 시 세로 자동
    void onHeightChanged(int h);       // 비율 고정 시 가로 자동
    void commitGeometry();             // 현 X/Y/W/H 를 모델에 반영
    void onAspectChanged();            // 화면 비율 X 또는 Y 편집 완료
    void onOpacitySliderReleased();
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
    QSpinBox*    m_x = nullptr;
    QSpinBox*    m_y = nullptr;
    QSpinBox*    m_w = nullptr;
    QSpinBox*    m_h = nullptr;
    QSpinBox*    m_aspectW    = nullptr;   // 화면 비율 X (예: 16)
    QSpinBox*    m_aspectH    = nullptr;   // 화면 비율 Y (예: 9)
    QCheckBox*   m_lockAspect = nullptr;   // 비율 고정

    // 투명도
    QSlider*     m_opacitySlider  = nullptr;   // 0..100 (%)
    QLabel*      m_opacityReadout = nullptr;

    // 표시 순서 (z-order)
    QPushButton* m_btnFront  = nullptr;
    QPushButton* m_btnRaise  = nullptr;
    QPushButton* m_btnLower  = nullptr;
    QPushButton* m_btnBack   = nullptr;
};

} // namespace uwp
