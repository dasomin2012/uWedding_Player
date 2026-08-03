#include "PageProperties.h"

#include "program/Program.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>

namespace uwp {

PageProperties::PageProperties(QWidget* parent)
    : QWidget(parent)
{
    m_name = new QLineEdit;
    m_name->setPlaceholderText(tr("(빈 값이면 Page 순번 표시)"));
    connect(m_name, &QLineEdit::editingFinished, this, [this]{
        if (m_loading) return;
        emit renameRequested(m_name->text());
    });

    m_displayTime = new QSpinBox;
    m_displayTime->setRange(0, 86400);   // 0=수동, 최대 하루
    m_displayTime->setSuffix(tr(" 초"));
    m_displayTime->setSpecialValueText(tr("수동 (0)"));
    connect(m_displayTime, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v){
                if (m_loading) return;
                emit displayTimeChanged(v);
            });

    // 페이지 카드는 좌→우 가로 스트립이므로 "위/아래" 대신 "앞/뒤" 사용.
    m_btnUp   = new QPushButton(tr("◀ 앞으로"));
    m_btnDown = new QPushButton(tr("뒤로 ▶"));
    connect(m_btnUp,   &QPushButton::clicked, this, [this]{
        if (!m_loading) emit moveUpRequested();
    });
    connect(m_btnDown, &QPushButton::clicked, this, [this]{
        if (!m_loading) emit moveDownRequested();
    });

    auto* form = new QFormLayout;
    form->addRow(tr("이름"), m_name);
    form->addRow(tr("표시 시간"), m_displayTime);

    auto* moveRow = new QHBoxLayout;
    moveRow->addWidget(m_btnUp);
    moveRow->addWidget(m_btnDown);

    auto* moveLabel = new QLabel(tr("페이지 이동"));
    moveLabel->setStyleSheet("font-weight: bold;");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 6, 8, 8);
    root->setSpacing(6);
    root->addLayout(form);
    root->addSpacing(4);
    root->addWidget(moveLabel);
    root->addLayout(moveRow);
    root->addStretch(1);

    setPage(nullptr);   // 초기 비활성
}

void PageProperties::setPage(const Page* page, int pageIndex, int pageCount) {
    m_loading = true;
    const bool has = (page != nullptr);
    m_name->setEnabled(has);
    m_displayTime->setEnabled(has);
    m_btnUp->setEnabled(has && pageIndex > 0);
    m_btnDown->setEnabled(has && pageIndex >= 0 && pageIndex + 1 < pageCount);
    if (has) {
        m_name->setText(page->name);
        m_displayTime->setValue(page->displayTimeSec);
    } else {
        m_name->clear();
        m_displayTime->setValue(0);
    }
    m_loading = false;
}

} // namespace uwp
