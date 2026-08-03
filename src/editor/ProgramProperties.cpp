#include "ProgramProperties.h"

#include "program/Program.h"

#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QSlider>
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

    // BGM — 프로그램 배경음.
    //   [파일 라벨(stretch)] [× 제거(파일 있을 때만)] [+ 추가]  구조.
    m_bgmPathLbl = new QLabel(tr("(없음)"));
    m_bgmPathLbl->setStyleSheet("color: #888;");
    m_bgmPathLbl->setWordWrap(false);
    m_bgmPathLbl->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_bgmClear = new QPushButton(QStringLiteral("×"));
    m_bgmClear->setToolTip(tr("BGM 제거"));
    m_bgmClear->setFixedSize(22, 22);
    m_bgmClear->setFlat(true);
    m_bgmClear->hide();   // 파일 있을 때만 노출
    connect(m_bgmClear, &QPushButton::clicked, this, &ProgramProperties::onBgmMenu);

    m_bgmAdd = new QPushButton(tr("+ 추가"));
    connect(m_bgmAdd, &QPushButton::clicked, this, &ProgramProperties::onAddBgm);

    m_bgmVolume = new QSlider(Qt::Horizontal);
    m_bgmVolume->setRange(0, 100);
    m_bgmVolLbl = new QLabel(QStringLiteral("80%"));
    m_bgmVolLbl->setMinimumWidth(36);
    m_bgmVolLbl->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    connect(m_bgmVolume, &QSlider::valueChanged, this, [this](int v){
        m_bgmVolLbl->setText(QStringLiteral("%1%").arg(v));
        if (!m_loading) emit bgmVolumeChanged(v);
    });

    m_bgmLoop = new QCheckBox(tr("반복 재생"));
    connect(m_bgmLoop, &QCheckBox::toggled, this, [this](bool on){
        if (!m_loading) emit bgmLoopChanged(on);
    });

    // 오디오 출력 장치 선택 — 볼륨 우측 스피커 버튼.
    m_bgmOutput = new QPushButton(QStringLiteral("🔊"));
    m_bgmOutput->setToolTip(tr("오디오 출력 장치 선택"));
    m_bgmOutput->setFixedSize(28, 22);
    m_bgmOutput->setEnabled(false);   // setAudioOutputs 후 활성
    connect(m_bgmOutput, &QPushButton::clicked, this, &ProgramProperties::showOutputMenu);

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

    // BGM 폼
    //   파일:   [파일명 stretch=1]  [× 제거]  [+ 추가]
    //   볼륨:   [슬라이더 stretch=1]  [40%]  [🔊 출력]
    auto* bgmFileRow = new QHBoxLayout;
    bgmFileRow->setContentsMargins(0, 0, 0, 0);
    bgmFileRow->setSpacing(4);
    bgmFileRow->addWidget(m_bgmPathLbl, 1);
    bgmFileRow->addWidget(m_bgmClear, 0);
    bgmFileRow->addWidget(m_bgmAdd,   0);

    auto* bgmVolRow = new QHBoxLayout;
    bgmVolRow->setContentsMargins(0, 0, 0, 0);
    bgmVolRow->setSpacing(6);
    bgmVolRow->addWidget(m_bgmVolume, 1);
    bgmVolRow->addWidget(m_bgmVolLbl, 0);
    bgmVolRow->addWidget(m_bgmOutput, 0);

    auto* bgmForm = new QFormLayout;
    bgmForm->addRow(tr("파일"), bgmFileRow);
    bgmForm->addRow(tr("볼륨"), bgmVolRow);
    bgmForm->addRow(tr(""),     m_bgmLoop);

    auto* bgmLabel = new QLabel(tr("BGM (배경음)"));
    bgmLabel->setStyleSheet("font-weight: bold;");

    auto* moveRow = new QHBoxLayout;
    moveRow->addWidget(m_btnUp);
    moveRow->addWidget(m_btnDown);

    auto* moveLabel = new QLabel(tr("프로그램 이동"));
    moveLabel->setStyleSheet("font-weight: bold;");

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(8, 6, 8, 8);
    root->setSpacing(6);
    root->addLayout(form);
    root->addSpacing(6);
    root->addWidget(bgmLabel);
    root->addLayout(bgmForm);
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
    m_bgmAdd->setEnabled(has);
    m_bgmVolume->setEnabled(has);
    m_bgmLoop->setEnabled(has);
    const bool hasBgm = has && program && !program->bgmPath.isEmpty();
    m_bgmClear->setVisible(hasBgm);
    m_btnUp->setEnabled(has && programIndex > 0);
    m_btnDown->setEnabled(has && programIndex >= 0
                              && programIndex + 1 < programCount);
    if (has) {
        m_name->setText(program->name);
        const int idx = m_endAction->findData(
            QVariant::fromValue(static_cast<int>(program->endAction)));
        if (idx >= 0) m_endAction->setCurrentIndex(idx);
        if (program->bgmPath.isEmpty()) {
            m_bgmPathLbl->setText(tr("(없음)"));
            m_bgmPathLbl->setStyleSheet("color:#888;");
        } else {
            m_bgmPathLbl->setText(QFileInfo(program->bgmPath).fileName());
            m_bgmPathLbl->setToolTip(program->bgmPath);
            m_bgmPathLbl->setStyleSheet("");
        }
        m_bgmVolume->setValue(program->bgmVolume);
        m_bgmVolLbl->setText(QStringLiteral("%1%").arg(program->bgmVolume));
        m_bgmLoop->setChecked(program->bgmLoop);
    } else {
        m_name->clear();
        m_endAction->setCurrentIndex(-1);
        m_bgmPathLbl->setText(tr("(없음)"));
        m_bgmPathLbl->setStyleSheet("color:#888;");
        m_bgmPathLbl->setToolTip(QString());
        m_bgmVolume->setValue(80);
        m_bgmLoop->setChecked(true);
    }
    m_loading = false;
}

void ProgramProperties::onAddBgm() {
    if (m_loading) return;
    const QString path = QFileDialog::getOpenFileName(
        this, tr("BGM 파일 선택"), QString(),
        tr("오디오 (*.mp3 *.wav *.m4a *.ogg *.flac *.aac);;모든 파일 (*)"));
    if (path.isEmpty()) return;
    m_bgmPathLbl->setText(QFileInfo(path).fileName());
    m_bgmPathLbl->setToolTip(path);
    m_bgmPathLbl->setStyleSheet("");
    m_bgmClear->setVisible(true);
    emit bgmPathChanged(path);
}

void ProgramProperties::onBgmMenu() {
    if (m_loading) return;
    m_bgmPathLbl->setText(tr("(없음)"));
    m_bgmPathLbl->setStyleSheet("color:#888;");
    m_bgmPathLbl->setToolTip(QString());
    m_bgmClear->setVisible(false);
    emit bgmPathChanged(QString());
}

void ProgramProperties::setAudioOutputs(
    const QList<QPair<QString, QString>>& devices, const QString& currentId)
{
    m_outputs = devices;
    m_outputCurrent = currentId;
    m_bgmOutput->setEnabled(!devices.isEmpty());
}

void ProgramProperties::showOutputMenu() {
    if (m_outputs.isEmpty()) return;
    QMenu menu(this);
    auto* group = new QActionGroup(&menu);
    group->setExclusive(true);
    // 첫 항목: 시스템 기본
    QAction* defAct = menu.addAction(tr("시스템 기본"));
    defAct->setCheckable(true);
    defAct->setChecked(m_outputCurrent.isEmpty());
    group->addAction(defAct);
    menu.addSeparator();
    for (const auto& dev : m_outputs) {
        QAction* act = menu.addAction(dev.second);
        act->setCheckable(true);
        act->setChecked(dev.first == m_outputCurrent);
        act->setData(dev.first);
        group->addAction(act);
    }
    QAction* chosen = menu.exec(m_bgmOutput->mapToGlobal(
        QPoint(0, m_bgmOutput->height())));
    if (!chosen) return;
    const QString id = (chosen == defAct) ? QString() : chosen->data().toString();
    if (id == m_outputCurrent) return;
    m_outputCurrent = id;
    emit audioOutputChanged(id);
}

} // namespace uwp
