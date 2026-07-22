#include "CollapsibleSection.h"

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QVBoxLayout>

namespace uwp {

CollapsibleSection::CollapsibleSection(const QString& title, QWidget* parent)
    : QFrame(parent)
{
    setObjectName("CollapsibleSection");
    setFrameShape(QFrame::NoFrame);

    // ▼/▶ 텍스트 토글 — QToolButton + AutoRaise 는 일부 QSS 조합에서
    // 화살표가 흐릿하게 렌더되는 회귀가 있어, 유니코드 화살표 QPushButton 이
    // 훨씬 견고하다. objectName 으로 QSS 대상 특정.
    m_toggle = new QPushButton(QStringLiteral("▼"));
    m_toggle->setObjectName("CollapsibleToggle");
    m_toggle->setFlat(true);
    m_toggle->setCursor(Qt::PointingHandCursor);
    m_toggle->setFixedSize(22, 22);
    m_toggle->setFocusPolicy(Qt::NoFocus);   // Tab 순회에서 제외 — 편집 흐름 보존

    m_title = new QLabel(title);
    m_title->setObjectName("CollapsibleTitle");
    m_title->setCursor(Qt::PointingHandCursor);
    m_title->installEventFilter(this);   // 라벨 클릭도 토글

    m_headerLayout = new QHBoxLayout;
    m_headerLayout->setContentsMargins(8, 4, 8, 4);   // 헤더 세로 ~40px (버튼 sizeHint 포함)
    m_headerLayout->setSpacing(6);
    m_headerLayout->addWidget(m_toggle);
    m_headerLayout->addWidget(m_title, 0);
    m_headerLayout->addStretch(1);

    // 헤더 프레임 — 배경/구분선 스타일과 클릭 이벤트 감지의 앵커.
    //   프레임 자체 여백에 클릭해도 토글되도록 eventFilter 로 잡는다.
    m_headerFrame = new QFrame;
    m_headerFrame->setObjectName("CollapsibleHeader");
    m_headerFrame->setLayout(m_headerLayout);
    m_headerFrame->setCursor(Qt::PointingHandCursor);
    m_headerFrame->installEventFilter(this);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);
    root->addWidget(m_headerFrame);

    connect(m_toggle, &QPushButton::clicked, this,
            [this]{ setExpanded(!m_expanded); });
}

void CollapsibleSection::setContent(QWidget* content) {
    if (m_content == content) return;
    if (m_content) {
        layout()->removeWidget(m_content);
        m_content->deleteLater();
    }
    m_content = content;
    if (m_content) {
        m_content->setParent(this);
        layout()->addWidget(m_content);
        m_content->setVisible(m_expanded);
    }
}

void CollapsibleSection::setHeaderRight(QWidget* widget) {
    if (m_headerRight == widget) return;
    if (m_headerRight) {
        m_headerLayout->removeWidget(m_headerRight);
        m_headerRight->deleteLater();
    }
    m_headerRight = widget;
    if (m_headerRight) {
        // stretch 뒤에 붙어 우측 정렬. 부모는 헤더 프레임으로 → 헤더 클릭
        // eventFilter 가 라벨/여백만 잡고 이 위젯(버튼 등)은 통과시킨다.
        m_headerRight->setParent(m_headerFrame);
        m_headerLayout->addWidget(m_headerRight);
    }
}

void CollapsibleSection::setExpanded(bool on) {
    if (m_expanded == on) return;
    m_expanded = on;
    updateArrow();
    if (m_content) m_content->setVisible(on);
    // 접힘 시 세션의 최대 높이를 헤더 크기로 고정 — 상위 QVBoxLayout 이
    // 잉여 세로 공간을 이 세션에 배정하지 못하게 막는다(빈 여백 방지).
    //  펼침 시 상한 해제해 컨텐츠가 자유롭게 성장.
    if (on) {
        setMaximumHeight(QWIDGETSIZE_MAX);
    } else if (m_headerFrame) {
        setMaximumHeight(m_headerFrame->sizeHint().height());
    }
    updateGeometry();
    emit expandedChanged(on);
}

void CollapsibleSection::updateArrow() {
    if (!m_toggle) return;
    m_toggle->setText(m_expanded ? QStringLiteral("▼") : QStringLiteral("▶"));
}

bool CollapsibleSection::eventFilter(QObject* obj, QEvent* ev) {
    // 헤더 프레임 여백 또는 타이틀 라벨 클릭 → 토글.
    //   프레임 위에 있는 QPushButton/QToolButton 등 자식은 자체 클릭을
    //   먼저 소비하므로 여기까지 오지 않는다 (ON/OFF 버튼은 정상 동작).
    if ((obj == m_headerFrame || obj == m_title)
        && ev->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(ev);
        if (me->button() == Qt::LeftButton) {
            setExpanded(!m_expanded);
            return true;
        }
    }
    return QFrame::eventFilter(obj, ev);
}

} // namespace uwp
