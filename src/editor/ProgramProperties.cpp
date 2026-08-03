#include "ProgramProperties.h"

#include "program/Program.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>

namespace uwp {

ProgramProperties::ProgramProperties(QWidget* parent)
    : QWidget(parent)
{
    m_name = new QLineEdit;
    connect(m_name, &QLineEdit::editingFinished, this, [this]{
        if (m_loading) return;
        emit renameRequested(m_name->text());
    });

    m_endAction = new QComboBox;
    // (사용자 표시 텍스트, EndAction 값) — displayText 는 다이얼로그 방식과 동일.
    m_endAction->addItem(tr("반복 재생 (같은 프로그램 반복)"),
                         QVariant::fromValue(static_cast<int>(EndAction::Loop)));
    m_endAction->addItem(tr("정지 (Live 검정)"),
                         QVariant::fromValue(static_cast<int>(EndAction::Stop)));
    m_endAction->addItem(tr("마지막 화면 유지"),
                         QVariant::fromValue(static_cast<int>(EndAction::Hold)));
    m_endAction->addItem(tr("다음 프로그램으로"),
                         QVariant::fromValue(static_cast<int>(EndAction::Next)));
    m_endAction->addItem(tr("첫 프로그램으로"),
                         QVariant::fromValue(static_cast<int>(EndAction::First)));
    connect(m_endAction, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx){
                if (m_loading || idx < 0) return;
                const int val = m_endAction->itemData(idx).toInt();
                emit endActionChanged(static_cast<EndAction>(val));
            });

    // 프로그램 카드도 좌→우 가로 스트립이라 페이지와 동일한 방향 규칙.
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
    form->addRow(tr("종료 동작"), m_endAction);

    auto* moveRow = new QHBoxLayout;
    moveRow->addWidget(m_btnUp);
    moveRow->addWidget(m_btnDown);

    auto* moveLabel = new QLabel(tr("프로그램 이동"));
    moveLabel->setStyleSheet("font-weight: bold;");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 6, 8, 8);
    root->setSpacing(6);
    root->addLayout(form);
    root->addSpacing(4);
    root->addWidget(moveLabel);
    root->addLayout(moveRow);
    root->addStretch(1);

    setProgram(nullptr);
}

void ProgramProperties::setProgram(const Program* program,
                                    int programIndex, int programCount) {
    m_loading = true;
    const bool has = (program != nullptr);
    m_name->setEnabled(has);
    m_endAction->setEnabled(has);
    m_btnUp->setEnabled(has && programIndex > 0);
    m_btnDown->setEnabled(has && programIndex >= 0
                              && programIndex + 1 < programCount);
    if (has) {
        m_name->setText(program->name);
        const int idx = m_endAction->findData(
            QVariant::fromValue(static_cast<int>(program->endAction)));
        if (idx >= 0) m_endAction->setCurrentIndex(idx);
    } else {
        m_name->clear();
        m_endAction->setCurrentIndex(-1);
    }
    m_loading = false;
}

} // namespace uwp
