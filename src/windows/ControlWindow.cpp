#include "ControlWindow.h"

#include "app/Settings.h"
#include "scene/SceneModel.h"
#include "editor/PreviewCanvas.h"
#include "editor/PropertyPanel.h"
#include "editor/MediaListWidget.h"
#include "program/ProgramListWidget.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>

namespace uwp {

namespace {

// ---- UI-B: QSS 테마 (밝은 / 어두운) --------------------------------
// 팔레트는 UI 초안 v0.2 와 일치. 위젯 스타일은 objectName 기반으로 분기.
const char* kQssLight = R"(
QMainWindow, QWidget#Central {
    background: #f7f2ec;
    color: #2c241f;
}
QWidget { font-size: 13px; color: #2c241f; }

QFrame#Panel {
    background: #ffffff;
    border: 1px solid #e6dfd6;
    border-radius: 10px;
}
QLabel#PanelHeader {
    color: #7a6f65;
    font-weight: 700;
    font-size: 12px;
    padding: 2px 2px 6px 2px;
    border-bottom: 1px solid #f0e8dc;
    margin-bottom: 4px;
}
QFrame#GlobalBar {
    background: #ffffff;
    border: 1px solid #e6dfd6;
    border-radius: 10px;
}

QPushButton {
    background: #fbf7f1;
    border: 1px solid #d9cfc2;
    border-radius: 6px;
    padding: 6px 12px;
    color: #2c241f;
}
QPushButton:hover { background: #efe2cf; }
QPushButton:pressed { background: #e9d9c3; }
QPushButton:disabled { color: #b0a695; background: #f2ede4; }

QPushButton#TakeButton {
    background: #c0392b;
    color: white;
    border: 0;
    border-radius: 8px;
    font-size: 16px;
    font-weight: 800;
    padding: 12px 32px;
    letter-spacing: 0.05em;
}
QPushButton#TakeButton:hover { background: #d64533; }
QPushButton#TakeButton:pressed { background: #a02b1f; }

QPushButton#FbtbButton {
    background: #1a1a1a;
    color: white;
    border: 0;
    border-radius: 8px;
    font-weight: 700;
    padding: 10px 18px;
}
QPushButton#FbtbButton:hover { background: #333; }

QPushButton#FillButton {
    background: #b98a5e;
    color: white;
    border: 0;
    border-radius: 6px;
    font-weight: 700;
    padding: 8px 12px;
}
QPushButton#FillButton:hover { background: #a67849; }
QPushButton#FillButton:pressed { background: #8f6538; }

QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {
    background: #ffffff;
    border: 1px solid #d9cfc2;
    border-radius: 5px;
    padding: 4px 8px;
    color: #2c241f;
    min-height: 22px;
    selection-background-color: #b98a5e;
    selection-color: white;
}
QComboBox::drop-down { border: 0; width: 20px; }

QSlider::groove:horizontal {
    background: #e6dfd6;
    height: 6px;
    border-radius: 3px;
}
QSlider::handle:horizontal {
    background: #b98a5e;
    width: 14px;
    margin: -5px 0;
    border-radius: 7px;
}
QSlider::sub-page:horizontal {
    background: #b98a5e;
    border-radius: 3px;
}

QSplitter::handle { background: transparent; }
QSplitter::handle:horizontal { width: 8px; }
QSplitter::handle:vertical { height: 8px; }

QMenuBar { background: transparent; padding: 4px; }
QMenuBar::item { padding: 5px 10px; border-radius: 5px; }
QMenuBar::item:selected { background: #efe2cf; }
QMenu {
    background: #ffffff;
    border: 1px solid #d9cfc2;
    padding: 4px;
    border-radius: 6px;
}
QMenu::item { padding: 6px 20px; border-radius: 4px; }
QMenu::item:selected { background: #efe2cf; }
QMenu::separator { height: 1px; background: #e6dfd6; margin: 4px 6px; }

QStatusBar { background: transparent; color: #7a6f65; }
QToolBar { background: transparent; border: 0; padding: 3px; spacing: 4px; }
QToolBar QToolButton {
    padding: 5px 10px;
    border-radius: 5px;
    background: transparent;
    color: #2c241f;
}
QToolBar QToolButton:hover { background: #efe2cf; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical {
    background: #d9cfc2;
    border-radius: 5px;
    min-height: 30px;
}
QScrollBar::handle:vertical:hover { background: #b98a5e; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal {
    background: #d9cfc2;
    border-radius: 5px;
    min-width: 30px;
}
QScrollBar::handle:horizontal:hover { background: #b98a5e; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

QLabel#LiveMirror { background: #111; color: #888; border-radius: 6px; }
)";

const char* kQssDark = R"(
QMainWindow, QWidget#Central {
    background: #1c1815;
    color: #f0e8dd;
}
QWidget { font-size: 13px; color: #f0e8dd; }

QFrame#Panel {
    background: #26221e;
    border: 1px solid #3a332d;
    border-radius: 10px;
}
QLabel#PanelHeader {
    color: #a89a8b;
    font-weight: 700;
    font-size: 12px;
    padding: 2px 2px 6px 2px;
    border-bottom: 1px solid #35302a;
    margin-bottom: 4px;
}
QFrame#GlobalBar {
    background: #26221e;
    border: 1px solid #3a332d;
    border-radius: 10px;
}

QPushButton {
    background: #2f2a25;
    border: 1px solid #4a3f34;
    border-radius: 6px;
    padding: 6px 12px;
    color: #f0e8dd;
}
QPushButton:hover { background: #4a3d2f; }
QPushButton:pressed { background: #5c4a37; }
QPushButton:disabled { color: #5c5346; background: #26221e; }

QPushButton#TakeButton {
    background: #e15a4c;
    color: white;
    border: 0;
    border-radius: 8px;
    font-size: 16px;
    font-weight: 800;
    padding: 12px 32px;
    letter-spacing: 0.05em;
}
QPushButton#TakeButton:hover { background: #ec7365; }
QPushButton#TakeButton:pressed { background: #b64536; }

QPushButton#FbtbButton {
    background: #0d0d0d;
    color: white;
    border: 0;
    border-radius: 8px;
    font-weight: 700;
    padding: 10px 18px;
}
QPushButton#FbtbButton:hover { background: #333; }

QPushButton#FillButton {
    background: #d4a373;
    color: #1c1815;
    border: 0;
    border-radius: 6px;
    font-weight: 700;
    padding: 8px 12px;
}
QPushButton#FillButton:hover { background: #e0b688; }
QPushButton#FillButton:pressed { background: #b8895e; }

QComboBox, QLineEdit, QSpinBox, QDoubleSpinBox {
    background: #2f2a25;
    border: 1px solid #4a3f34;
    border-radius: 5px;
    padding: 4px 8px;
    color: #f0e8dd;
    min-height: 22px;
    selection-background-color: #d4a373;
    selection-color: #1c1815;
}
QComboBox::drop-down { border: 0; width: 20px; }

QSlider::groove:horizontal {
    background: #3a332d;
    height: 6px;
    border-radius: 3px;
}
QSlider::handle:horizontal {
    background: #d4a373;
    width: 14px;
    margin: -5px 0;
    border-radius: 7px;
}
QSlider::sub-page:horizontal {
    background: #d4a373;
    border-radius: 3px;
}

QSplitter::handle { background: transparent; }
QSplitter::handle:horizontal { width: 8px; }
QSplitter::handle:vertical { height: 8px; }

QMenuBar { background: transparent; padding: 4px; color: #f0e8dd; }
QMenuBar::item { padding: 5px 10px; border-radius: 5px; }
QMenuBar::item:selected { background: #4a3d2f; }
QMenu {
    background: #26221e;
    border: 1px solid #4a3f34;
    padding: 4px;
    border-radius: 6px;
    color: #f0e8dd;
}
QMenu::item { padding: 6px 20px; border-radius: 4px; }
QMenu::item:selected { background: #4a3d2f; }
QMenu::separator { height: 1px; background: #3a332d; margin: 4px 6px; }

QStatusBar { background: transparent; color: #a89a8b; }
QToolBar { background: transparent; border: 0; padding: 3px; spacing: 4px; }
QToolBar QToolButton {
    padding: 5px 10px;
    border-radius: 5px;
    background: transparent;
    color: #f0e8dd;
}
QToolBar QToolButton:hover { background: #4a3d2f; }

QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }
QScrollBar::handle:vertical {
    background: #4a3f34;
    border-radius: 5px;
    min-height: 30px;
}
QScrollBar::handle:vertical:hover { background: #d4a373; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }
QScrollBar::handle:horizontal {
    background: #4a3f34;
    border-radius: 5px;
    min-width: 30px;
}
QScrollBar::handle:horizontal:hover { background: #d4a373; }
QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }

QLabel#LiveMirror { background: #000; color: #666; border-radius: 6px; }
)";

// ---- 패널 래퍼 헬퍼: 내부 위젯을 둥근 프레임 + 선택적 상단 헤더에 담아 반환 ----
// title 이 비면 헤더 없이 프레임만 (내부 위젯이 자체 제목을 가진 경우 사용).
QWidget* wrapPanel(QWidget* content, const QString& title = QString()) {
    auto* frame = new QFrame;
    frame->setObjectName("Panel");
    auto* v = new QVBoxLayout(frame);
    v->setContentsMargins(10, 10, 10, 10);
    v->setSpacing(6);
    if (!title.isEmpty()) {
        auto* h = new QLabel(title);
        h->setObjectName("PanelHeader");
        v->addWidget(h);
    }
    v->addWidget(content, 1);
    return frame;
}

} // namespace

// ---- ControlWindow ----------------------------------------------
ControlWindow::ControlWindow(Settings* settings, SceneModel* scene,
                             SnapshotCache* snapshots, QWidget* parent)
    : QMainWindow(parent)
    , m_settings(settings)
    , m_scene(scene)
    , m_snapshots(snapshots)
{
    setWindowTitle("uWeddingPlayer — Control");
    resize(1600, 940);

    createMenus();
    createCentralLayout();

    // UI-B: 저장된 테마 로드 후 적용.
    QSettings qs("Hanmac", "uWeddingPlayer");
    const QString saved = qs.value("ui/theme", "light").toString();
    applyTheme(saved);

    statusBar()->showMessage("Ready");
}

ControlWindow::~ControlWindow() = default;

void ControlWindow::createMenus() {
    auto* fileMenu    = menuBar()->addMenu(tr("&File"));
    auto* settingsAct = fileMenu->addAction(tr("&Settings..."));
    connect(settingsAct, &QAction::triggered,
            this, &ControlWindow::openSettingsRequested);
    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction(tr("E&xit"));
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    auto* sceneMenu = menuBar()->addMenu(tr("&Scene"));
    auto* saveAct   = sceneMenu->addAction(tr("&Save Scene"));
    saveAct->setShortcut(QKeySequence::Save);
    connect(saveAct, &QAction::triggered, this, &ControlWindow::saveSceneRequested);
    auto* loadAct = sceneMenu->addAction(tr("&Load Scene"));
    connect(loadAct, &QAction::triggered, this, &ControlWindow::loadSceneRequested);
    sceneMenu->addSeparator();
    auto* clearAct = sceneMenu->addAction(tr("&Clear Scene"));
    connect(clearAct, &QAction::triggered, this, [this]{ m_scene->clear(); });

    auto* toolsMenu  = menuBar()->addMenu(tr("&Tools"));
    auto* monitorAct = toolsMenu->addAction(tr("Select Output &Monitor..."));
    connect(monitorAct, &QAction::triggered,
            this, &ControlWindow::selectOutputMonitorRequested);
    auto* playTestAct = toolsMenu->addAction(tr("Play &Test Video..."));
    connect(playTestAct, &QAction::triggered,
            this, &ControlWindow::playTestVideoRequested);
    toolsMenu->addSeparator();

    // UI-B: 다크 모드 토글
    m_darkThemeAct = toolsMenu->addAction(tr("&다크 모드"));
    m_darkThemeAct->setCheckable(true);
    m_darkThemeAct->setShortcut(QKeySequence("Ctrl+Shift+D"));
    connect(m_darkThemeAct, &QAction::triggered, this, &ControlWindow::toggleTheme);

    auto* toolbar = addToolBar(tr("Main"));
    toolbar->setMovable(false);
    toolbar->addAction(saveAct);
    toolbar->addAction(loadAct);
    toolbar->addAction(clearAct);
    toolbar->addSeparator();
    toolbar->addAction(monitorAct);
    toolbar->addAction(playTestAct);
}

// ================================================================
// UI-A: 3-컬럼 레이아웃
//   +-----------+---------------------+-----------+
//   |           |   Preview Canvas    |  Live     |
//   | Media     |                     |  Mirror   |
//   | Library   +---------------------+           |
//   | (세로)    |                     +-----------+
//   |           |   Programs / Pages  |           |
//   |           |                     | Property  |
//   +-----------+---------------------+-----------+
//   | GlobalBar: F2B / TAKE / 전환 / BGM              |
//   +-------------------------------------------------+
// ================================================================
void ControlWindow::createCentralLayout() {
    // ----- 미디어 리스트 -----
    m_mediaList = new MediaListWidget(m_settings, m_snapshots);
    m_mediaList->setMinimumWidth(200);
    connect(m_mediaList, &MediaListWidget::mediaActivated,
            this, [this](const QString& path) {
                const QSize cs = m_scene->canvasSize();
                const qreal w = cs.width()  * 0.4;
                const qreal h = cs.height() * 0.4;
                m_scene->addLayer(path,
                    QRectF((cs.width() - w) / 2.0,
                           (cs.height() - h) / 2.0, w, h));
            });

    // ----- Preview 캔버스 -----
    m_canvas = new PreviewCanvas(m_scene, m_snapshots);

    // ----- Live 미러 -----
    m_liveMirror = new QLabel;
    m_liveMirror->setObjectName("LiveMirror");
    m_liveMirror->setAlignment(Qt::AlignCenter);
    m_liveMirror->setMinimumSize(240, 135);   // 16:9 mini

    // ----- Programs (기존 위젯 재사용; UI-C/D 에서 카드형으로 교체 예정) -----
    m_programList = new ProgramListWidget;
    m_programList->setMinimumHeight(140);

    // ----- Property Panel -----
    m_property = new PropertyPanel(m_scene);

    // ----- TAKE 버튼 + 전환 콤보 (하단 GlobalBar 로 이동) -----
    m_takeButton = new QPushButton(tr("TAKE"));
    m_takeButton->setObjectName("TakeButton");
    m_takeButton->setMinimumSize(200, 52);
    connect(m_takeButton, &QPushButton::clicked,
            this, &ControlWindow::takeRequested);

    m_takeMode = new QComboBox;
    m_takeMode->addItems({ "cut", "fade" });
    m_takeMode->setMinimumWidth(90);
    {
        const QString dm = m_settings ? m_settings->takeDefaultMode().toLower()
                                      : QString("fade");
        m_takeMode->setCurrentText(dm == "cut" ? "cut" : "fade");
    }
    connect(m_takeMode, &QComboBox::currentTextChanged,
            this, &ControlWindow::takeModeChanged);

    // ----- F2B 버튼 + BGM 슬라이더 (placeholder) -----
    m_btnFbtb = new QPushButton(tr("⬛ 검정 (F2B)"));
    m_btnFbtb->setObjectName("FbtbButton");
    m_btnFbtb->setMinimumWidth(140);
    m_btnFbtb->setEnabled(false);   // UI-A: 자리만 확보, 동작은 후속 단계
    m_btnFbtb->setToolTip(tr("응급 검은 화면 송출 (구현 예정)"));

    m_bgmSlider = new QSlider(Qt::Horizontal);
    m_bgmSlider->setRange(0, 100);
    m_bgmSlider->setValue(70);
    m_bgmSlider->setFixedWidth(140);
    m_bgmSlider->setEnabled(false);   // UI-A: 자리만 확보, BGM 파이프라인은 후속
    m_bgmSlider->setToolTip(tr("BGM 볼륨 (구현 예정)"));

    // ----- 컬럼 조립 -----

    // LEFT: 미디어 (자체 헤더가 있으므로 외부 타이틀 없이 프레임만)
    auto* leftPanel = wrapPanel(m_mediaList);

    // CENTER TOP: Preview
    auto* previewPanel = wrapPanel(m_canvas, tr("작업 캔버스"));

    // CENTER BOTTOM: Programs (자체 헤더 있음)
    auto* programPanel = wrapPanel(m_programList);

    auto* centerCol = new QSplitter(Qt::Vertical);
    centerCol->addWidget(previewPanel);
    centerCol->addWidget(programPanel);
    centerCol->setStretchFactor(0, 3);
    centerCol->setStretchFactor(1, 1);
    centerCol->setSizes({ 620, 200 });

    // RIGHT TOP: Live mirror
    auto* livePanel = wrapPanel(m_liveMirror, tr("Live 송출"));

    // RIGHT BOTTOM: Property (자체 헤더 있음)
    auto* propPanel = wrapPanel(m_property);

    auto* rightCol = new QSplitter(Qt::Vertical);
    rightCol->addWidget(livePanel);
    rightCol->addWidget(propPanel);
    rightCol->setStretchFactor(0, 0);
    rightCol->setStretchFactor(1, 1);
    rightCol->setSizes({ 220, 600 });

    // OUTER: 3-column horizontal splitter
    auto* outer = new QSplitter(Qt::Horizontal);
    outer->addWidget(leftPanel);
    outer->addWidget(centerCol);
    outer->addWidget(rightCol);
    outer->setStretchFactor(0, 0);
    outer->setStretchFactor(1, 1);
    outer->setStretchFactor(2, 0);
    outer->setSizes({ 260, 940, 320 });

    // GlobalBar
    m_globalBar = buildGlobalBar();

    // ROOT
    auto* central = new QWidget(this);
    central->setObjectName("Central");
    auto* root = new QVBoxLayout(central);
    root->setContentsMargins(6, 6, 6, 6);
    root->setSpacing(6);
    root->addWidget(outer, 1);
    root->addWidget(m_globalBar);
    setCentralWidget(central);
}

QWidget* ControlWindow::buildGlobalBar() {
    auto* bar = new QFrame;
    bar->setObjectName("GlobalBar");
    auto* h = new QHBoxLayout(bar);
    h->setContentsMargins(14, 10, 14, 10);
    h->setSpacing(14);

    // 좌: F2B
    h->addWidget(m_btnFbtb);

    // 좌우 균형용 스트레치
    h->addStretch(1);

    // 중앙: TAKE + 안내 라벨
    auto* takeCol = new QVBoxLayout;
    takeCol->setSpacing(2);
    takeCol->setContentsMargins(0, 0, 0, 0);
    auto* takeHint = new QLabel(tr("Preview → Live 로 송출"));
    takeHint->setAlignment(Qt::AlignCenter);
    takeHint->setStyleSheet("color: palette(mid); font-size: 11px;");
    takeCol->addWidget(takeHint);
    takeCol->addWidget(m_takeButton, 0, Qt::AlignCenter);
    h->addLayout(takeCol);

    h->addStretch(1);

    // 우: 전환 콤보
    auto* transLabel = new QLabel(tr("전환"));
    transLabel->setStyleSheet("color: palette(mid);");
    h->addWidget(transLabel);
    h->addWidget(m_takeMode);

    // 우: BGM
    auto* bgmLabel = new QLabel(tr("🎵 BGM"));
    bgmLabel->setStyleSheet("color: palette(mid);");
    h->addWidget(bgmLabel);
    h->addWidget(m_bgmSlider);

    return bar;
}

// ---- UI-B: 테마 적용 / 토글 ----------------------------------------
void ControlWindow::applyTheme(const QString& theme) {
    m_currentTheme = (theme == "dark") ? "dark" : "light";
    const char* qss = (m_currentTheme == "dark") ? kQssDark : kQssLight;
    qApp->setStyleSheet(QString::fromUtf8(qss));
    if (m_darkThemeAct)
        m_darkThemeAct->setChecked(m_currentTheme == "dark");
}

void ControlWindow::toggleTheme() {
    const QString next = (m_currentTheme == "dark") ? "light" : "dark";
    applyTheme(next);
    QSettings qs("Hanmac", "uWeddingPlayer");
    qs.setValue("ui/theme", next);
}

void ControlWindow::setStatusText(const QString& text) {
    statusBar()->showMessage(text);
}

} // namespace uwp
