#include "PropertyPanel.h"

#include "scene/SceneModel.h"

#include <QCheckBox>
#include <QComboBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPair>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QVariant>
#include <QtMath>

namespace uwp {

// 비율 정규화 — 운영자가 "일반 화면비 대비 얼마나 다른가"를 즉시 읽도록
// 16 또는 9 중 하나를 축으로 고정해서 (X, Y) 정수 쌍으로 반환.
//   ratio ≥ 16/9 (와이드)  → (X, 9)     (예: 16:9, 21:9, 32:9)
//   1  ≤ ratio < 16/9      → (16, Y)    (예: 16:10, 16:12 = 4:3)
//   9/16 < ratio < 1       → (Y, 16)    (세로 방향)
//   ratio ≤ 9/16           → (9, X)
// 표준 프리셋(1% 이내)이면 관용 표기(예: 4:3, 3:2, 1:1)를 우선.
static QPair<int, int> normalizeAspect(double ratio) {
    if (!qIsFinite(ratio) || ratio <= 0) return {0, 0};
    struct Preset { double r; int x, y; };
    static const Preset presets[] = {
        {16.0/9.0,   16, 9}, {21.0/9.0,  21, 9}, {32.0/9.0,  32, 9},
        {16.0/10.0, 16, 10}, {4.0/3.0,    4, 3}, {3.0/2.0,    3, 2},
        {5.0/4.0,    5, 4},  {1.0,        1, 1},
        {9.0/16.0,   9, 16}, {10.0/16.0, 10, 16}, {3.0/4.0,   3, 4},
    };
    for (const auto& p : presets)
        if (qAbs(ratio - p.r) / p.r < 0.01) return {p.x, p.y};
    if (ratio >= 16.0 / 9.0) return {qRound(ratio * 9.0), 9};
    if (ratio >= 1.0)         return {16, qRound(16.0 / ratio)};
    if (ratio >= 9.0 / 16.0)  return {qRound(ratio * 16.0), 16};
    return {9, qRound(9.0 / ratio)};
}

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

    // 화면 비율 — X, Y 각각 별도 편집박스. 기본 16:9.
    m_aspectW = new QSpinBox;
    m_aspectW->setRange(1, 1000);
    m_aspectW->setValue(16);
    m_aspectW->setFixedWidth(56);
    m_aspectW->setAlignment(Qt::AlignRight);
    m_aspectH = new QSpinBox;
    m_aspectH->setRange(1, 1000);
    m_aspectH->setValue(9);
    m_aspectH->setFixedWidth(56);
    m_aspectH->setAlignment(Qt::AlignRight);
    m_aspectW->setToolTip(tr("화면 비율의 가로 값"));
    m_aspectH->setToolTip(tr("화면 비율의 세로 값"));

    m_lockAspect = new QCheckBox("비율 고정");
    m_lockAspect->setChecked(true);

    // ----- 투명도 -----
    m_opacityReadout = new QLabel("투명도: 100%");
    m_opacitySlider  = new QSlider(Qt::Horizontal);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(100);

    // ----- 표시 순서 (z-order) -----
    m_btnFront = new QPushButton("맨 앞으로");
    m_btnRaise = new QPushButton("한 칸 앞으로");
    m_btnLower = new QPushButton("한 칸 뒤로");
    m_btnBack  = new QPushButton("맨 뒤로");
    auto* zrow1 = new QHBoxLayout; zrow1->addWidget(m_btnFront); zrow1->addWidget(m_btnRaise);
    auto* zrow2 = new QHBoxLayout; zrow2->addWidget(m_btnLower); zrow2->addWidget(m_btnBack);

    // ----- 레이아웃 -----
    auto* form = new QFormLayout;
    form->addRow("미디어", m_media);
    form->addRow("이름",  m_name);

    auto* root = new QVBoxLayout(this);
    root->addWidget(title);
    root->addLayout(form);
    root->addWidget(new QLabel("위치 / 크기"));
    root->addLayout(geo);
    // 한 줄: [화면 비율 라벨] [X] : [Y]  ────────  [☑ 비율 고정]
    auto* aspRow = new QHBoxLayout;
    aspRow->setContentsMargins(0, 0, 0, 0);
    aspRow->setSpacing(4);
    aspRow->addWidget(new QLabel(tr("화면 비율")));
    aspRow->addSpacing(4);
    aspRow->addWidget(m_aspectW);
    auto* colon = new QLabel(":");
    colon->setStyleSheet("font-weight: 700;");
    aspRow->addWidget(colon);
    aspRow->addWidget(m_aspectH);
    aspRow->addStretch(1);
    aspRow->addWidget(m_lockAspect);
    root->addLayout(aspRow);
    root->addSpacing(6);
    root->addWidget(m_opacityReadout);
    root->addWidget(m_opacitySlider);
    root->addWidget(new QLabel("표시 순서"));
    root->addLayout(zrow1);
    root->addLayout(zrow2);
    root->addStretch();
    setMinimumWidth(240);

    // model -> panel
    connect(m_model, &SceneModel::selectionChanged,
            this, &PropertyPanel::onSelectionChanged);
    connect(m_model, &SceneModel::layerChanged,
            this, &PropertyPanel::onLayerChanged);

    // panel -> model
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

    connect(m_btnFront, &QPushButton::clicked, this, [this]{ if(!m_id.isEmpty()) m_model->toFront(m_id); });
    connect(m_btnRaise, &QPushButton::clicked, this, [this]{ if(!m_id.isEmpty()) m_model->raise(m_id); });
    connect(m_btnLower, &QPushButton::clicked, this, [this]{ if(!m_id.isEmpty()) m_model->lower(m_id); });
    connect(m_btnBack,  &QPushButton::clicked, this, [this]{ if(!m_id.isEmpty()) m_model->toBack(m_id); });

    // "비율 고정" 상태를 모델에 동기화 → 마우스 드래그 리사이즈(LayerItem)도
    // 같은 플래그를 읽어 종횡비를 유지한다(스피너 연동과 단일 소스).
    m_model->setAspectLocked(m_lockAspect->isChecked());
    connect(m_lockAspect, &QCheckBox::toggled, this,
            [this](bool on){ m_model->setAspectLocked(on); });

    // 화면 비율 X 또는 Y 편집 → W 유지, H 재계산.
    //  editingFinished 는 스핀 up/down 클릭 · 텍스트 편집 후 focus 이탈/
    //  Enter 시 모두 발화. valueChanged 를 쓰면 사용자가 두 자리 숫자를
    //  타이핑 중 매 키마다 지오메트리가 튀므로 editingFinished 로 확정.
    connect(m_aspectW, &QSpinBox::editingFinished,
            this, &PropertyPanel::onAspectChanged);
    connect(m_aspectH, &QSpinBox::editingFinished,
            this, &PropertyPanel::onAspectChanged);

    setEnabledAll(false);
}

void PropertyPanel::setEnabledAll(bool on) {
    QWidget* widgets[] = {
        (QWidget*)m_name,
        (QWidget*)m_x, (QWidget*)m_y, (QWidget*)m_w, (QWidget*)m_h,
        (QWidget*)m_aspectW, (QWidget*)m_aspectH,
        (QWidget*)m_lockAspect, (QWidget*)m_opacitySlider,
        (QWidget*)m_btnFront, (QWidget*)m_btnRaise,
        (QWidget*)m_btnLower, (QWidget*)m_btnBack,
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

    const int opPct = qBound(0, qRound(l->opacity * 100.0), 100);
    m_opacitySlider->setValue(opPct);
    m_opacityReadout->setText(QString("투명도: %1%").arg(opPct));

    // 화면 비율 — 현재 W/H 로부터 계산해 정규화된 X/Y 로 표시.
    if (l->geometry.height() > 0.5) {
        const auto ap = normalizeAspect(
            l->geometry.width() / l->geometry.height());
        if (ap.first > 0 && ap.second > 0) {
            m_aspectW->setValue(ap.first);
            m_aspectH->setValue(ap.second);
        }
    }
    m_loading = false;
}

// ---- 위치 / 크기 ----------------------------------------------
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

// X 또는 Y 편집 완료 → W 유지, H 재계산.
void PropertyPanel::onAspectChanged() {
    if (m_loading || m_id.isEmpty()) return;
    const int ax = m_aspectW->value();
    const int ay = m_aspectH->value();
    if (ax <= 0 || ay <= 0) return;   // 스핀박스 min=1 이라 사실상 도달 X
    const Layer* l = m_model->layer(m_id);
    if (!l) return;
    const double r = static_cast<double>(ax) / static_cast<double>(ay);
    const qreal w = l->geometry.width();
    const qreal h = qMax<qreal>(1.0, qRound(w / r));
    m_model->setGeometry(m_id, QRectF(l->geometry.x(), l->geometry.y(), w, h));
}

void PropertyPanel::onOpacitySliderReleased() {
    if (m_loading || m_id.isEmpty()) return;
    m_model->setOpacity(m_id, m_opacitySlider->value() / 100.0);
}

// ---- 공통 ------------------------------------------------------
void PropertyPanel::commitName() {
    if (m_loading || m_id.isEmpty()) return;
    m_model->setName(m_id, m_name->text());
}

} // namespace uwp
