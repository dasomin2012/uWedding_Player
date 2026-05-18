#pragma once

#include <QWidget>
#include <QString>

class QLineEdit;
class QSpinBox;
class QDoubleSpinBox;
class QComboBox;
class QLabel;
class QPushButton;

namespace uwp {

class SceneModel;

// 선택된 Layer 의 속성을 양방향 편집.
// 모델 selectionChanged/layerChanged 를 구독해 필드 갱신,
// 필드 편집 시 모델에 기록 (m_loading 가드로 에코 방지).
class PropertyPanel : public QWidget {
    Q_OBJECT
public:
    PropertyPanel(SceneModel* model, QWidget* parent = nullptr);

private slots:
    void onSelectionChanged(const QString& id);
    void onLayerChanged(const QString& id);

    void commitGeometry();
    void commitOpacity(double v);
    void commitDisplayTime(int v);
    void commitEndAction(int idx);
    void commitName();

private:
    void loadFrom(const QString& id);
    void setEnabledAll(bool on);

    SceneModel*     m_model    = nullptr;
    QString         m_id;             // 현재 편집 대상
    bool            m_loading  = false;

    QLabel*         m_media    = nullptr;
    QLineEdit*      m_name     = nullptr;
    QSpinBox*       m_x        = nullptr;
    QSpinBox*       m_y        = nullptr;
    QSpinBox*       m_w        = nullptr;
    QSpinBox*       m_h        = nullptr;
    QDoubleSpinBox* m_opacity  = nullptr;
    QSpinBox*       m_display  = nullptr;
    QComboBox*      m_endAction= nullptr;
    QPushButton*    m_btnFront = nullptr;
    QPushButton*    m_btnRaise = nullptr;
    QPushButton*    m_btnLower = nullptr;
    QPushButton*    m_btnBack  = nullptr;
    QPushButton*    m_btnDelete= nullptr;
};

} // namespace uwp
