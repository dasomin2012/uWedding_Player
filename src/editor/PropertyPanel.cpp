#include "PropertyPanel.h"

#include "scene/SceneModel.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QSlider>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QVariant>

namespace uwp {

PropertyPanel::PropertyPanel(SceneModel* model, QWidget* parent)
    : QWidget(parent)
    , m_model(model)
{
    auto* title = new QLabel("속성");
    title->setStyleSheet("font-weight: bold;");

    m_media = new QLabel("(선택된 레이어 없음)");
    m_media->setWordWrap(true);
    m_media->setStyleSheet("color:#888;");
    m_name = new QLineEdit;

    // ----- 위치 / 크기 -----
    m_btnFill = new QPushButton("꽉 채우기");
    m_btnFill->setMinimumHeight(36);
    m_btnFill->setStyleSheet("background:#cfe2ff; font-weight:bold;");

    auto mkSpin = [](int lo, int hi) {
        auto* s = new QSpinBox;
        s->setRange(lo, hi);
        s->setSingleStep(1);
        return s;
    };
    m_x = mkSpin(-100000, 100000);
    m_y = mkSpin(-100000, 100000);
    m_w = mkSpin(1, 100000);
    m_h = mkSpin(1, 100000);

    auto* geo = new QGridLayout;
    geo->addWidget(new QLabel("왼쪽(X)"), 0, 0); geo->addWidget(m_x, 0, 1);
    geo->addWidget(new QLabel("위(Y)"),   0, 2); geo->addWidget(m_y, 0, 3);
    geo->addWidget(new QLabel("가로(W)"), 1, 0); geo->addWidget(m_w, 1, 1);
    geo->addWidget(new QLabel("세로(H)"), 1, 2); geo->addWidget(m_h, 1, 3);

    m_lockAspect = new QCheckBox("비율 고정 (가로↔세로 함께 조정)");
    m_lockAspect->setChecked(true);

    // ----- 투명도 -----
    m_opacityReadout = new QLabel("투명도: 100%");
    m_opacitySlider  = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(100);

    // ----- 공통 -----
    m_display = mkSpin(0, 86400);
    m_display->setSuffix(" 초");

    m_endAction = new QComboBox;
    m_endAction->addItem("반복 재생",         QVariant::fromValue(EndAction::Loop));
    m_endAction->addItem("정지",              QVariant::fromValue(EndAction::Stop));
    m_endAction->addItem("마지막 화면 유지",  QVariant::fromValue(EndAction::Hold));
    m_endAction->addItem("다음 프로그램으로", QVariant::fromValue(EndAction::Next));

    m_btnFront = new QPushButton("맨 앞으로");
    m_btnRaise = new QPushButton("한 칸 앞으로");
    m_btnLower = new QPushButton("한 칸 뒤로");
    m_btnBack  = new QPushButton("맨 뒤로");
    auto* zrow1 = new QHBoxLayout; zrow1->addWidget(m_btnFront); zrow1->addWidget(m_btnRaise);
    auto* zrow2 = new QHBoxLayout; zrow2->addWidget(m_btnLower); zrow2->addWidget(m_btnBack);

    m_btnDelete = new QPushButton("레이어 삭제");
    m_btnDelete->setStyleSheet("color:#c33;");

    // ----- 레이아웃 -----
    auto* form = new QFormLayout;
    form->addRow("미디어", m_media);
    form->addRow("이름",  m_name);

    auto* root = new QVBoxLayout(this);
    root->addWidget(title);
    root->addLayout(form);
    root->addWidget(m_btnFill);
    root->addWidget(new QLabel("위치 / 크기"));
    root->addLayout(geo);
    root->addWidget(m_lockAspect);
    root->addSpacing(6);
    root->addWidget(m_opacityReadout);
    root->addWidget(m_opacitySlider);
    auto* form2 = new QFormLayout;
    form2->addRow("표시 시간", m_display);
    form2->addRow("재생 끝나면", m_endAction);
    root->addLayout(form2);
    root->addWidget(new QLabel("표시 순서"));
    root->addLayout(zrow1);
    root->addLayout(zrow2);
    root->addSpacing(8);
    root->addWidget(m_btnDelete);
    root->addStretch();
    setMinimumWidth(240);

    // model -> panel
    connect(m_model, &SceneModel::selectionChanged,
            this, &PropertyPanel::onSelectionChanged);
    connect(m_model, &SceneModel::layerChanged,
            this, &PropertyPanel::onLayerChanged);

    // panel -> model
    connect(m_btnFill, &QPushButton::clicked, this, &PropertyPanel::onFillClicked);
    connect(m_x, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PropertyPanel::commitGeometry);
    connect(m_y, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PropertyPanel::commitGeometry);
    connect(m_w, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PropertyPanel::onWidthChanged);
    connect(m_h, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PropertyPanel::onHeightChanged);

    connect(m_opacitySlider, &QSlider::valueChanged, this,
            [this](int v){ m_opacityReadout->setText(QString("투명도: %1%").arg(v)); });
    connect(m_opacitySlider, &QSlider::sliderReleased,
            this, &PropertyPanel::onOpacitySliderReleased);

    connect(m_name, &QLineEdit::editingFinished,
            this, &PropertyPanel::commitName);
    connect(m_display, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &PropertyPanel::commitDisplayTime);
    connect(m_endAction, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &PropertyPanel::commitEndAction);

    connect(m_btnFront, &QPushButton::clicked, this, [this]{ if(!m_id.isEmpty()) m_model->toFront(m_id); });
    connect(m_btnRaise, &QPushButton::clicked, this, [this]{ if(!m_id.isEmpty()) m_model->raise(m_id); });
    connect(m_btnLower, &QPushButton::clicked, this, [this]{ if(!m_id.isEmpty()) m_model->lower(m_id); });
    connect(m_btnBack,  &QPushButton::clicked, this, [this]{ if(!m_id.isEmpty()) m_model->toBack(m_id); });
    connect(m_btnDelete,&QPushButton::clicked, this, [this]{ if(!m_id.isEmpty()) m_model->removeLayer(m_id); });

    // "비율 고정" 상태를 모델에 동기화 → 마우스 드래그 리사이즈(LayerItem)도
    // 같은 플래그를 읽어 종횡비를 유지한다(스피너 연동과 단일 소스).
    m_model->setAspectLocked(m_lockAspect->isChecked());
    connect(m_lockAspect, &QCheckBox::toggled, this,
            [this](bool on){ m_model->setAspectLocked(on); });

    setEnabledAll(false);
}

void PropertyPanel::setEnabledAll(bool on) {
    QWidget* widgets[] = {
        (QWidget*)m_name, (QWidget*)m_btnFill,
        (QWidget*)m_x, (QWidget*)m_y, (QWidget*)m_w, (QWidget*)m_h,
        (QWidget*)m_lockAspect, (QWidget*)m_opacitySlider,
        (QWidget*)m_display, (QWidget*)m_endAction,
        (QWidget*)m_btnFront, (QWidget*)m_btnRaise,
        (QWidget*)m_btnLower, (QWidget*)m_btnBack, (QWidget*)m_btnDelete,
    };
    for (QWidget* w : widgets) if (w) w->setEnabled(on);
}

void PropertyPanel::onSelectionChanged(const QString& id) {
    m_id = id;
    if (id.isEmpty()) {
        m_media->setText("(선택된 레이어 없음)");
        setEnabledAll(false);
        return;
    }
    setEnabledAll(true);
    loadFrom(id);
}

void PropertyPanel::onLayerChanged(const QString& id) {
    if (id == m_id && !m_loading) loadFrom(id);
}

void PropertyPanel::loadFrom(const QString& id) {
    const Layer* l = m_model->layer(id);
    if (!l) return;
    m_loading = true;
    m_media->setText(l->media);
    m_name->setText(l->name);
    m_x->setValue(qRound(l->geometry.x()));
    m_y->setValue(qRound(l->geometry.y()));
    m_w->setValue(qRound(l->geometry.width()));
    m_h->setValue(qRound(l->geometry.height()));
    m_display->setValue(l->displayTimeSec);
    const int eaIdx = m_endAction->findData(QVariant::fromValue(l->endAction));
    if (eaIdx >= 0) m_endAction->setCurrentIndex(eaIdx);

    const int opPct = qBound(0, qRound(l->opacity * 100.0), 100);
    m_opacitySlider->setValue(opPct);
    m_opacityReadout->setText(QString("투명도: %1%").arg(opPct));
    m_loading = false;
}

// ---- 위치 / 크기 ----------------------------------------------
void PropertyPanel::onFillClicked() {
    if (m_id.isEmpty()) return;
    const QSize c = m_model->canvasSize();
    m_model->setGeometry(m_id, QRectF(0, 0, c.width(), c.height()));
}

void PropertyPanel::onWidthChanged(int w) {
    if (m_loading || m_id.isEmpty()) return;
    if (m_lockAspect && m_lockAspect->isChecked()) {
        const Layer* l = m_model->layer(m_id);
        if (l && l->geometry.height() > 0.5) {
            const double aspect = l->geometry.width() / l->geometry.height();
            if (aspect > 0.0001) {
                m_loading = true;                       // 연동 setValue 의 재진입 차단
                m_h->setValue(qMax(1, qRound(w / aspect)));
                m_loading = false;
            }
        }
    }
    commitGeometry();
}

void PropertyPanel::onHeightChanged(int h) {
    if (m_loading || m_id.isEmpty()) return;
    if (m_lockAspect && m_lockAspect->isChecked()) {
        const Layer* l = m_model->layer(m_id);
        if (l && l->geometry.width() > 0.5) {
            const double aspect = l->geometry.width() / l->geometry.height();
            m_loading = true;
            m_w->setValue(qMax(1, qRound(h * aspect)));
            m_loading = false;
        }
    }
    commitGeometry();
}

void PropertyPanel::commitGeometry() {
    if (m_loading || m_id.isEmpty()) return;
    m_model->setGeometry(m_id, QRectF(m_x->value(), m_y->value(),
                                      m_w->value(), m_h->value()));
}

void PropertyPanel::onOpacitySliderReleased() {
    if (m_loading || m_id.isEmpty()) return;
    m_model->setOpacity(m_id, m_opacitySlider->value() / 100.0);
}

// ---- 공통 ------------------------------------------------------
void PropertyPanel::commitDisplayTime(int v) {
    if (m_loading || m_id.isEmpty()) return;
    m_model->setDisplayTime(m_id, v);
}

void PropertyPanel::commitEndAction(int idx) {
    if (m_loading || m_id.isEmpty() || idx < 0) return;
    m_model->setEndAction(m_id, m_endAction->itemData(idx).value<EndAction>());
}

void PropertyPanel::commitName() {
    if (m_loading || m_id.isEmpty()) return;
    m_model->setName(m_id, m_name->text());
}

} // namespace uwp
