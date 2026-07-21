#include "DisplaySettingsDialog.h"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QScreen>
#include <QSpinBox>
#include <QVBoxLayout>

namespace uwp {

DisplaySettingsDialog::DisplaySettingsDialog(QWidget* parent,
                                             const QString& currentMode,
                                             int currentMonitorIndex,
                                             const QRect& currentGeometry)
    : QDialog(parent) {
    setWindowTitle(tr("디스플레이 설정"));
    setModal(true);
    resize(460, 320);

    // ---- 모드 선택 라디오 ----
    m_radioMonitor = new QRadioButton(tr("모니터 모드 — 선택한 모니터 전체 사용"), this);
    m_radioScreen  = new QRadioButton(tr("스크린 모드 — 좌표와 크기 지정"),        this);
    auto* modeGroup = new QButtonGroup(this);
    modeGroup->addButton(m_radioMonitor);
    modeGroup->addButton(m_radioScreen);
    if (currentMode == QStringLiteral("screen"))
        m_radioScreen->setChecked(true);
    else
        m_radioMonitor->setChecked(true);

    // ---- 위젯 ----
    m_monitor = new QComboBox(this);
    m_x = new QSpinBox(this);  m_y = new QSpinBox(this);
    m_w = new QSpinBox(this);  m_h = new QSpinBox(this);

    // 다중 모니터 좌측/상단이 있으면 음수 좌표가 정상. 넉넉한 범위.
    // 해상도(W×H) 상한 = libobs 코어 상한 16384. obs-websocket 은 라이브
    // SetVideoSettings 를 4096 으로 캡하지만, 우리는 OBS 프로파일 basic.ini
    // 사전 기록으로 우회한다(ObsProcessManager::writeCanvasToProfileIni).
    // → 4096 초과 값은 OBS 다음 기동부터 반영(현재 세션은 4096 라이브 캡 유지).
    m_x->setRange(-32768, 32768);  m_y->setRange(-32768, 32768);
    m_w->setRange(64, 16384);       m_h->setRange(64, 16384);
    m_x->setSuffix(" px");  m_y->setSuffix(" px");
    m_w->setSuffix(" px");  m_h->setSuffix(" px");
    m_x->setValue(currentGeometry.x());
    m_y->setValue(currentGeometry.y());
    m_w->setValue(currentGeometry.width());
    m_h->setValue(currentGeometry.height());

    populateMonitors(currentMonitorIndex);

    // ---- 레이아웃 ----
    auto* monBox = new QGroupBox(tr("모니터"), this);
    {
        auto* v = new QVBoxLayout(monBox);
        v->addWidget(m_monitor);
    }

    auto* screenBox = new QGroupBox(tr("스크린 영역 (데스크톱 좌표)"), this);
    {
        auto* form = new QFormLayout(screenBox);
        auto* xy = new QHBoxLayout;
        xy->addWidget(new QLabel(QStringLiteral("X")));
        xy->addWidget(m_x);
        xy->addSpacing(12);
        xy->addWidget(new QLabel(QStringLiteral("Y")));
        xy->addWidget(m_y);
        xy->addStretch();
        auto* wh = new QHBoxLayout;
        wh->addWidget(m_w);
        wh->addWidget(new QLabel(QStringLiteral("×")));
        wh->addWidget(m_h);
        wh->addStretch();
        form->addRow(tr("시작 좌표:"),   xy);
        form->addRow(tr("스크린 해상도:"), wh);
    }

    m_hint = new QLabel(this);
    m_hint->setStyleSheet(QStringLiteral("color:#8a7d70;"));
    m_hint->setWordWrap(true);

    auto* buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button(QDialogButtonBox::Ok)->setText(tr("확인"));
    buttons->button(QDialogButtonBox::Cancel)->setText(tr("취소"));
    connect(buttons, &QDialogButtonBox::accepted,
            this, &DisplaySettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected,
            this, &DisplaySettingsDialog::reject);

    auto* v = new QVBoxLayout(this);
    v->addWidget(m_radioMonitor);
    v->addWidget(m_radioScreen);
    v->addWidget(monBox);
    v->addWidget(screenBox);
    v->addWidget(m_hint);
    v->addStretch();
    v->addWidget(buttons);

    // ---- 상호 배타 활성화 ----
    auto onModeChanged = [this]{ updateEnabledStates(); };
    connect(m_radioMonitor, &QRadioButton::toggled, this, onModeChanged);
    connect(m_radioScreen,  &QRadioButton::toggled, this, onModeChanged);
    updateEnabledStates();
}

QString DisplaySettingsDialog::mode() const {
    return m_radioScreen->isChecked() ? QStringLiteral("screen")
                                      : QStringLiteral("monitor");
}

int DisplaySettingsDialog::monitorIndex() const {
    return m_monitor->currentData().toInt();
}

QRect DisplaySettingsDialog::outputGeometry() const {
    if (mode() == QStringLiteral("screen")) {
        return { m_x->value(), m_y->value(), m_w->value(), m_h->value() };
    }
    // 모니터 모드: 선택 모니터의 실제 geometry 로 채워 반환 → 호출자가
    // 캔버스 사이즈/씬 재커밋 시 그 값을 그대로 사용.
    const int idx = monitorIndex();
    const auto screens = QGuiApplication::screens();
    if (idx >= 0 && idx < screens.size()) return screens[idx]->geometry();
    return { m_x->value(), m_y->value(), m_w->value(), m_h->value() };
}

void DisplaySettingsDialog::updateEnabledStates() {
    const bool monitorMode = m_radioMonitor->isChecked();
    m_monitor->setEnabled(monitorMode);
    m_x->setEnabled(!monitorMode);
    m_y->setEnabled(!monitorMode);
    m_w->setEnabled(!monitorMode);
    m_h->setEnabled(!monitorMode);
    if (monitorMode) {
        m_hint->setText(tr("선택한 모니터의 전체 화면을 채워 송출합니다. "
                           "스크린 필드는 참고용 (선택 모니터 크기)."));
        // 선택된 모니터의 값을 프리뷰로 채움 (편집은 비활성).
        const int idx = monitorIndex();
        const auto screens = QGuiApplication::screens();
        if (idx >= 0 && idx < screens.size()) {
            const QRect g = screens[idx]->geometry();
            QSignalBlocker b1(m_x), b2(m_y), b3(m_w), b4(m_h);
            m_x->setValue(g.x());       m_y->setValue(g.y());
            m_w->setValue(g.width());   m_h->setValue(g.height());
        }
    } else {
        m_hint->setText(tr("데스크톱 가상 좌표계에서 지정한 영역에만 송출합니다. "
                           "LED 스크린이 특정 모니터의 부분 영역이거나 여러 "
                           "모니터에 걸쳐 있을 때 사용. "
                           "해상도 4096 초과는 다음 OBS 재기동부터 적용됩니다."));
    }
}

void DisplaySettingsDialog::populateMonitors(int currentIdx) {
    m_monitor->clear();
    const auto screens = QGuiApplication::screens();
    for (int i = 0; i < screens.size(); ++i) {
        const QRect g = screens[i]->geometry();
        const QString label = QStringLiteral("%1: %2 (%3 × %4 @ %5,%6)")
            .arg(i)
            .arg(screens[i]->name())
            .arg(g.width()).arg(g.height())
            .arg(g.x()).arg(g.y());
        m_monitor->addItem(label, i);
    }
    for (int r = 0; r < m_monitor->count(); ++r) {
        if (m_monitor->itemData(r).toInt() == currentIdx) {
            m_monitor->setCurrentIndex(r);
            break;
        }
    }
    // 모니터 콤보가 바뀌면 모니터 모드 프리뷰 값도 갱신.
    connect(m_monitor, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ updateEnabledStates(); });
}

} // namespace uwp
