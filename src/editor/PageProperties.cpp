#include "PageProperties.h"

#include "live/ILiveSink.h"          // primaryVideoIndex
#include "player/MediaDurationProbe.h"
#include "program/Program.h"

#include <QComboBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QVariant>
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

    // 표시 시간 — [재생 길이 | 시간 지정] + 초. 저장값 0 = 재생 길이.
    m_timeMode = new QComboBox;
    m_timeMode->addItem(tr("재생 길이"));
    m_timeMode->addItem(tr("시간 지정"));
    m_timeMode->setToolTip(tr("재생 길이: 동영상이 끝나면 종료 동작 실행\n"
                              "(동영상이 없는 페이지는 계속 표시)"));
    m_displayTime = new QSpinBox;
    m_displayTime->setRange(1, 86400);   // 최대 하루
    m_displayTime->setSuffix(tr(" 초"));
    m_displayTime->setValue(10);
    // 재생 길이 모드에서는 초 입력 대신 대표 영상의 실제 길이를 보여 준다.
    m_videoLen = new QLabel;
    m_videoLen->setMinimumWidth(80);
    m_probe = new MediaDurationProbe(this);
    connect(m_probe, &MediaDurationProbe::durationReady,
            this, [this](const QString& path, qint64){
                if (path == m_videoPath) updateTimeWidgets();
            });
    connect(m_timeMode, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx){
                updateTimeWidgets();
                if (m_loading || idx < 0) return;
                emit displayTimeChanged(idx == 1 ? m_displayTime->value() : 0);
            });
    connect(m_displayTime, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v){
                if (m_loading || m_timeMode->currentIndex() != 1) return;
                emit displayTimeChanged(v);
            });

    // 종료 동작 — 프로그램 속성과 같은 스타일 (표시 텍스트, EndAction 값).
    m_endAction = new QComboBox;
    m_endAction->addItem(tr("반복 재생 (이 페이지 반복)"),
                         QVariant::fromValue(static_cast<int>(EndAction::Loop)));
    m_endAction->addItem(tr("다음 페이지로"),
                         QVariant::fromValue(static_cast<int>(EndAction::Next)));
    m_endAction->addItem(tr("첫 페이지로"),
                         QVariant::fromValue(static_cast<int>(EndAction::First)));
    m_endAction->addItem(tr("정지 (Live 검정)"),
                         QVariant::fromValue(static_cast<int>(EndAction::Stop)));
    m_endAction->setToolTip(tr("마지막 페이지에서 \"다음 페이지로\" 는 "
                               "프로그램 종료 동작을 따릅니다."));
    connect(m_endAction, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx){
                if (m_loading || idx < 0) return;
                emit endActionChanged(
                    static_cast<EndAction>(m_endAction->itemData(idx).toInt()));
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
    auto* timeRow = new QHBoxLayout;
    timeRow->setContentsMargins(0, 0, 0, 0);
    timeRow->setSpacing(6);
    timeRow->addWidget(m_timeMode, 1);
    timeRow->addWidget(m_displayTime, 1);
    timeRow->addWidget(m_videoLen, 1);
    form->addRow(tr("표시 시간"), timeRow);
    form->addRow(tr("종료 동작"), m_endAction);

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
    m_timeMode->setEnabled(has);
    m_endAction->setEnabled(has);
    m_btnUp->setEnabled(has && pageIndex > 0);
    m_btnDown->setEnabled(has && pageIndex >= 0 && pageIndex + 1 < pageCount);
    if (has) {
        m_name->setText(page->name);
        const bool timed = page->displayTimeSec > 0;
        m_timeMode->setCurrentIndex(timed ? 1 : 0);
        if (timed) m_displayTime->setValue(page->displayTimeSec);
        const int ea = m_endAction->findData(
            QVariant::fromValue(static_cast<int>(page->endAction)));
        m_endAction->setCurrentIndex(ea);   // Hold 등 미노출 값이면 -1(빈칸)
        const int vi = primaryVideoIndex(page->layers);
        m_videoPath = (vi >= 0) ? page->layers[vi].media : QString();
    } else {
        m_name->clear();
        m_timeMode->setCurrentIndex(0);
        m_endAction->setCurrentIndex(-1);
        m_videoPath.clear();
    }
    updateTimeWidgets();
    m_loading = false;
}

void PageProperties::updateTimeWidgets() {
    const bool timed = (m_timeMode->currentIndex() == 1);
    m_displayTime->setVisible(timed);
    m_displayTime->setEnabled(timed && m_timeMode->isEnabled());
    m_videoLen->setVisible(!timed);
    if (timed) return;
    if (!m_timeMode->isEnabled()) { m_videoLen->clear(); return; }
    if (m_videoPath.isEmpty()) {
        m_videoLen->setText(tr("영상 없음"));
        m_videoLen->setToolTip(tr("동영상이 없는 페이지는 계속 표시됩니다."));
        return;
    }
    m_videoLen->setToolTip(m_videoPath);
    const qint64 ms = m_probe->duration(m_videoPath);   // 미확인이면 분석 요청
    if (ms > 0) {
        const qint64 sec = (ms + 500) / 1000;
        m_videoLen->setText(sec >= 3600
            ? QString("%1:%2:%3").arg(sec / 3600).arg(sec / 60 % 60, 2, 10, QChar('0'))
                                 .arg(sec % 60, 2, 10, QChar('0'))
            : QString("%1:%2").arg(sec / 60, 2, 10, QChar('0'))
                              .arg(sec % 60, 2, 10, QChar('0')));
    } else if (ms < 0) {
        m_videoLen->setText(tr("길이 확인 불가"));
    } else {
        m_videoLen->setText(tr("확인 중…"));
    }
}

} // namespace uwp
