#pragma once

#include <QWidget>
#include <QString>

class QLineEdit;
class QPlainTextEdit;
class QSpinBox;
class QComboBox;
class QFontComboBox;
class QGroupBox;
class QLabel;
class QPushButton;
class QSlider;
class QCheckBox;

namespace uwp {

class SceneModel;

// 선택된 Layer 의 속성을 양방향 편집.
//
// 이 위젯 자체(=미디어 속성 컨텐츠):
//   - 이름, 위치/크기(왼쪽/위/가로/세로), 화면 비율(X:Y), 비율 고정
//   - 투명도 슬라이더(%), 표시 순서(z-order)
//
// textContent() 로 노출되는 별도 위젯(=자막 속성 컨텐츠):
//   - 내용/글꼴/크기/굵게/정렬/글자색/배경색+투명도/여백
//
// 상위(ControlWindow)가 두 컨텐츠 위젯을 각각 CollapsibleSection 에 담아
// 우측 컬럼에 배치한다. selection/layerChanged 구독은 이 클래스가 통합
// 관리 — 텍스트 필드 편집도 텍스트 위젯이 살아있는 한 이 클래스가 처리.
//
// 표시 시간 · 종료 동작은 Program 단위 개념으로 통일 — 프로그램 카드에서 편집.
// 레이어 삭제 · 꽉 채우기는 Preview 툴바(오른쪽 아이콘)로 이동.
//
// 모델 selectionChanged/layerChanged 를 구독해 필드 갱신,
// 필드 편집 시 모델에 기록 (m_loading 가드로 에코/루프 방지).
class PropertyPanel : public QWidget {
    Q_OBJECT
public:
    PropertyPanel(SceneModel* model, QWidget* parent = nullptr);

    // 자막 속성 컨텐츠 — 최초엔 이 위젯의 자식이지만, CollapsibleSection 이
    // setContent 로 재부모(setParent) 하여 우측 컬럼의 별도 섹션에 배치한다.
    QWidget* textContent() const { return m_textContent; }

    // 현재 선택된 레이어가 텍스트 타입인지 — ControlWindow 자동 접힘 로직용.
    bool isTextLayerSelected() const;

private slots:
    void onSelectionChanged(const QString& id);
    void onLayerChanged(const QString& id);

    void onWidthChanged(int w);        // 비율 고정 시 세로 자동
    void onHeightChanged(int h);       // 비율 고정 시 가로 자동
    void commitGeometry();             // 현 X/Y/W/H 를 모델에 반영
    void onAspectChanged();            // 화면 비율 X 또는 Y 편집 완료
    void onOpacitySliderReleased();
    void commitName();
    // 텍스트 편집 슬롯 — 값이 실제로 바뀔 때만 모델에 기록.
    void commitText();
    void pickTextColor();
    void pickBgColor();

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

    // ---- 텍스트 위젯 편집 (mediaType == Text 일 때만 표시) ----
    // 자막·안내문 위주 정적 텍스트. 마키 스크롤은 v2 로 보류.
    //   과거엔 QGroupBox 한 개로 감쌌으나 우측 컬럼의 CollapsibleSection 이
    //   그 역할을 대신하므로, 컨텐츠 위젯(QWidget) 자체를 노출한다.
    QWidget*        m_textContent    = nullptr;   // 자막 속성 폼 전체 컨테이너
    QPlainTextEdit* m_textEdit       = nullptr;   // 여러 줄 입력
    QFontComboBox*  m_textFont       = nullptr;
    QSpinBox*       m_textSize       = nullptr;   // px
    QCheckBox*      m_textBold       = nullptr;   // 700 ↔ 400
    QComboBox*      m_textHAlign     = nullptr;   // 0/1/2 = 좌/중/우
    QComboBox*      m_textVAlign     = nullptr;   // 0/1/2 = 상/중/하
    QPushButton*    m_textColorBtn   = nullptr;   // 색 견본 — QColorDialog 트리거
    QPushButton*    m_bgColorBtn     = nullptr;
    QSlider*        m_bgOpacitySlider= nullptr;
    QLabel*         m_bgOpacityReadout = nullptr;
    QSpinBox*       m_textPadding    = nullptr;   // px
};

} // namespace uwp
